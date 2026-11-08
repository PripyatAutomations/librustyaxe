/* SPDX-License-Identifier: MIT
 * Nonblocking native WebSocket client transport; single-threaded poll owner.
 * DNS lookup is performed during connection establishment.
 */
#include <librustyaxe/transport.wss.h>
#include <wslay/wslay.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <pthread.h>

#define RR_WSS_HTTP_MAX 8192
#define RR_WSS_TX_MAX 65536
#define RR_WSS_FRAME_MAX 65536
enum { RESOLVING, CONNECTING, TLS_HANDSHAKE, HTTP_SEND, HTTP_READ, WEBSOCKET, DEAD };
typedef struct native_wss {
    rr_transport_t *transport;
    int fd, resolver_fd, stage, hops;
    SSL_CTX *ctx;
    SSL *ssl;
    wslay_event_context_ptr ws;
    char url[1024], host[256], path[768], origin[384], user_agent[384];
    char key[32];
    unsigned port;
    bool tls, allow_cross_origin, dead, closing, error_sent;
    char http[RR_WSS_HTTP_MAX + 1];
    size_t http_len;
    uint8_t rx[RR_WSS_HTTP_MAX];
    size_t rx_len, rx_off;
    uint64_t rx_bytes, tx_bytes;
    uint8_t tx[RR_WSS_TX_MAX];
    size_t tx_len, tx_offset;
    char reason[160];
} native_wss_t;

static void error(native_wss_t *c, const char *msg) {
    if (c->dead) return;
    snprintf(c->reason, sizeof(c->reason), "%s", msg);
    if (!c->error_sent) {
        c->error_sent = true;
        rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_ERROR, NULL,
                          c->reason, strlen(c->reason));
    }
    c->dead = true;
}
static void reset_socket(native_wss_t *c) {
    if (c->ws) { wslay_event_context_free(c->ws); c->ws = NULL; }
    if (c->ssl) { SSL_free(c->ssl); c->ssl = NULL; }
    if (c->ctx) { SSL_CTX_free(c->ctx); c->ctx = NULL; }
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
    if (c->resolver_fd >= 0) { close(c->resolver_fd); c->resolver_fd = -1; }
    c->tx_len = c->tx_offset = c->http_len = c->rx_len = c->rx_off = 0;
}
static bool parse_url(native_wss_t *c, const char *url) {
    bool tls;
    const char *p;
    if (!strncmp(url, "wss://", 6)) { tls = true; p = url + 6; }
    else if (!strncmp(url, "ws://", 5)) { tls = false; p = url + 5; }
    else return false;
    const char *end = p + strcspn(p, "/?#");
    if (end == p || end - p >= (int)sizeof(c->origin)) return false;
    if (memchr(p, '@', (size_t)(end - p))) return false;
    const char *host = p, *portp = NULL;
    size_t hostlen;
    if (*p == '[') {
        host = p + 1;
        const char *bracket = memchr(host, ']', (size_t)(end - host));
        if (!bracket) return false;
        hostlen = (size_t)(bracket - host);
        if (bracket + 1 != end) {
            if (bracket[1] != ':') return false;
            portp = bracket + 2;
        }
    } else {
        const char *colon = memchr(p, ':', (size_t)(end - p));
        hostlen = colon ? (size_t)(colon - p) : (size_t)(end - p);
        if (colon) portp = colon + 1;
    }
    if (!hostlen || hostlen >= sizeof(c->host)) return false;
    memcpy(c->host, host, hostlen);
    c->host[hostlen] = 0;
    c->port = tls ? 443 : 80;
    if (portp) {
        unsigned long port = 0;
        if (portp == end) return false;
        for (const char *q = portp; q < end; ++q) {
            if (*q < '0' || *q > '9') return false;
            port = port * 10 + (unsigned)(*q - '0');
            if (port > 65535) return false;
        }
        if (!port) return false;
        c->port = (unsigned)port;
    }
    const char *path = end;
    if (*path == '?' || *path == '#') return false;
    if (!*path) path = "/";
    if (strlen(path) >= sizeof(c->path) || strlen(url) >= sizeof(c->url)) return false;
    for (const char *q = path; *q; ++q) if (*q == '\r' || *q == '\n' || *q == '#') return false;
    snprintf(c->origin, sizeof(c->origin), "%.*s", (int)(end - url), url);
    snprintf(c->path, sizeof(c->path), "%s", path);
    snprintf(c->url, sizeof(c->url), "%s", url);
    c->tls = tls;
    return true;
}
/* Resolving in a detached worker avoids blocking the poll/audio thread.
 * The worker owns its task and one socketpair end, never touches native_wss.
 * File descriptors are process-wide: transferring the integer is sufficient.
 */
typedef struct resolver_task {
    char host[256], port[12];
    int notify;
} resolver_task_t;
static void *resolve_worker(void *opaque) {
    resolver_task_t *task=opaque;
    struct addrinfo hint={.ai_family=AF_UNSPEC,.ai_socktype=SOCK_STREAM};
    struct addrinfo *list=NULL;
    int fd=-1;
    if(!getaddrinfo(task->host,task->port,&hint,&list)) {
        for(struct addrinfo *a=list;a;a=a->ai_next) {
            fd=socket(a->ai_family,a->ai_socktype|SOCK_NONBLOCK|SOCK_CLOEXEC,a->ai_protocol);
            if(fd<0)continue;
            int rc=connect(fd,a->ai_addr,a->ai_addrlen);
            if(rc==0 || errno==EINPROGRESS)break;
            close(fd);fd=-1;
        }
        freeaddrinfo(list);
    }
    if(send(task->notify,&fd,sizeof(fd),MSG_NOSIGNAL)!=(ssize_t)sizeof(fd) && fd>=0)close(fd);
    close(task->notify);
    free(task);
    return NULL;
}
static bool start(native_wss_t *c) {
    int pair[2];
    if(socketpair(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0,pair)<0)return false;
    resolver_task_t *task=calloc(1,sizeof(*task));
    if(!task) {close(pair[0]);close(pair[1]);return false;}
    snprintf(task->host,sizeof(task->host),"%s",c->host);
    snprintf(task->port,sizeof(task->port),"%u",c->port);
    task->notify=pair[1];
    pthread_t thread;
    if(pthread_create(&thread,NULL,resolve_worker,task)) {
        close(pair[0]);close(pair[1]);free(task);return false;
    }
    pthread_detach(thread);
    c->resolver_fd=pair[0];
    c->stage=RESOLVING;
    return true;
}
static bool resolver_ready(native_wss_t *c,int timeout_ms) {
    struct pollfd pfd={.fd=c->resolver_fd,.events=POLLIN};
    int ready=poll(&pfd,1,timeout_ms);
    if(ready<=0)return false;
    int fd=-1;
    ssize_t n=recv(c->resolver_fd,&fd,sizeof(fd),0);
    close(c->resolver_fd);c->resolver_fd=-1;
    if(n!=(ssize_t)sizeof(fd) || fd<0) {
        error(c,"WebSocket DNS resolution / TCP connection failed");
        return false;
    }
    c->fd=fd;
    c->stage=CONNECTING;
    return true;
}
static bool setup_tls(native_wss_t *c) {
    c->ctx = SSL_CTX_new(TLS_client_method());
    if (!c->ctx || !SSL_CTX_set_default_verify_paths(c->ctx)) return false;
    SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER, NULL);
    c->ssl = SSL_new(c->ctx);
    if (!c->ssl || !SSL_set_fd(c->ssl, c->fd)) return false;
    if (!SSL_set_tlsext_host_name(c->ssl, c->host)) return false;
    if (!SSL_set1_host(c->ssl, c->host)) return false;
    SSL_set_connect_state(c->ssl);
    return true;
}
static int read_bytes(native_wss_t *c, void *buf, size_t len) {
    int n = c->ssl ? SSL_read(c->ssl, buf, (int)len) : (int)recv(c->fd, buf, len, 0);
    if (n > 0) { c->rx_bytes += (uint64_t)n; return n; }
    if (c->ssl) {
        int e = SSL_get_error(c->ssl, n);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return -2;
        return -1;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return -2;
    return -1;
}
static int flush_tx(native_wss_t *c) {
    while (c->tx_offset < c->tx_len) {
        const uint8_t *p = c->tx + c->tx_offset;
        size_t left = c->tx_len - c->tx_offset;
        int n = c->ssl ? SSL_write(c->ssl, p, (int)left) : (int)send(c->fd, p, left, MSG_NOSIGNAL);
        if (n > 0) { c->tx_offset += (size_t)n; c->tx_bytes += (uint64_t)n; continue; }
        if (c->ssl) {
            int e = SSL_get_error(c->ssl, n);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return 0;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
        return -1;
    }
    c->tx_len = c->tx_offset = 0;
    return 1;
}
static bool queue_bytes(native_wss_t *c, const void *data, size_t len) {
    size_t pending = c->tx_len - c->tx_offset;
    if (len > sizeof(c->tx) - pending) return false;
    if (pending && c->tx_offset) memmove(c->tx, c->tx + c->tx_offset, pending);
    c->tx_len = pending;
    c->tx_offset = 0;
    memcpy(c->tx + c->tx_len, data, len);
    c->tx_len += len;
    return true;
}
static ssize_t ws_read(wslay_event_context_ptr ws, uint8_t *buf, size_t len, int flags, void *arg) {
    (void)flags;
    native_wss_t *c = arg;
    if (c->rx_off < c->rx_len) {
        size_t available=c->rx_len-c->rx_off;
        if (len>available) len=available;
        memcpy(buf,c->rx+c->rx_off,len);
        c->rx_off+=len;
        if (c->rx_off==c->rx_len) c->rx_off=c->rx_len=0;
        return (ssize_t)len;
    }
    int n = read_bytes(c, buf, len);
    if (n == -2) { wslay_event_set_error(ws, WSLAY_ERR_WOULDBLOCK); return -1; }
    if (n < 0) { wslay_event_set_error(ws, WSLAY_ERR_CALLBACK_FAILURE); return -1; }
    return n;
}
static ssize_t ws_write(wslay_event_context_ptr ws, const uint8_t *data, size_t len, int flags, void *arg) {
    (void)flags;
    native_wss_t *c = arg;
    if (!queue_bytes(c, data, len)) { wslay_event_set_error(ws, WSLAY_ERR_WOULDBLOCK); return -1; }
    return (ssize_t)len;
}
static int ws_mask(wslay_event_context_ptr ws, uint8_t *buf, size_t len, void *arg) {
    (void)ws; (void)arg;
    return RAND_bytes(buf, (int)len) == 1 ? 0 : -1;
}
static void ws_message(wslay_event_context_ptr ws, const struct wslay_event_on_msg_recv_arg *msg, void *arg) {
    (void)ws;
    native_wss_t *c = arg;
    if (msg->opcode == WSLAY_TEXT_FRAME || msg->opcode == WSLAY_BINARY_FRAME) {
        rr_transport_emit(c->transport,
            msg->opcode == WSLAY_TEXT_FRAME ? RR_TRANSPORT_EVENT_MESSAGE : RR_TRANSPORT_EVENT_BINARY_MESSAGE,
            NULL, msg->msg, msg->msg_length);
    } else if (msg->opcode == WSLAY_PONG) {
        rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_PONG, NULL, msg->msg, msg->msg_length);
    } else if (msg->opcode == WSLAY_CONNECTION_CLOSE) {
        c->closing = true;
    }
}
static bool begin_upgrade(native_wss_t *c) {
    uint8_t nonce[16];
    if (RAND_bytes(nonce, sizeof(nonce)) != 1) return false;
    EVP_EncodeBlock((unsigned char *)c->key, nonce, sizeof(nonce));
    char request[2300];
    int n = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: %s\r\nUser-Agent: %s\r\n\r\n",
        c->path, c->host, c->port, c->key, c->user_agent);
    if (n < 0 || n >= (int)sizeof(request)) return false;
    if (!queue_bytes(c, request, (size_t)n)) return false;
    c->stage = HTTP_SEND;
    return true;
}
static bool redirect(native_wss_t *c, const char *location) {
    if (c->hops >= 8) return false;
    char next[1024];
    if (!strncasecmp(location, "https://", 8)) snprintf(next, sizeof(next), "wss://%s", location + 8);
    else if (!strncasecmp(location, "http://", 7)) snprintf(next, sizeof(next), "ws://%s", location + 7);
    else if (!strncasecmp(location, "wss://", 6) || !strncasecmp(location, "ws://", 5))
        snprintf(next, sizeof(next), "%s", location);
    else if (*location == '/') snprintf(next, sizeof(next), "%s%s", c->origin, location);
    else return false;
    if (!strcmp(next, c->url) || strlen(next) >= sizeof(c->url)) return false;
    char oldorigin[sizeof(c->origin)];
    snprintf(oldorigin, sizeof(oldorigin), "%s", c->origin);
    char oldpath[sizeof(c->path)];
    snprintf(oldpath, sizeof(oldpath), "%s", c->path);
    native_wss_t parsed = {0};
    if (!parse_url(&parsed, next)) return false;
    if (!c->allow_cross_origin && strcasecmp(parsed.origin, oldorigin)) return false;
    if (parsed.path[0] == '/' && parsed.path[1] == '\0' && strcmp(oldpath, "/")) {
        /* Proxy redirects often point to the authority root: keep Kiwi route. */
        if (strlen(next) + strlen(oldpath) - 1 >= sizeof(next)) return false;
        strcat(next, oldpath + 1);
        if (!parse_url(&parsed, next)) return false;
    }
    reset_socket(c);
    snprintf(c->url, sizeof(c->url), "%s", parsed.url);
    snprintf(c->host, sizeof(c->host), "%s", parsed.host);
    snprintf(c->path, sizeof(c->path), "%s", parsed.path);
    snprintf(c->origin, sizeof(c->origin), "%s", parsed.origin);
    c->port = parsed.port;
    c->tls = parsed.tls;
    c->hops++;
    return start(c);
}
static int inspect_response(native_wss_t *c) {
    char *end = strstr(c->http, "\r\n\r\n");
    if (!end) return 0;
    int status = 0;
    if (sscanf(c->http, "HTTP/%*u.%*u %d", &status) != 1) { error(c, "Invalid HTTP upgrade response"); return -1; }
    char location[1024] = "";
    char accept[128] = "";
    bool upgrade = false, connection = false;
    const char *p = strstr(c->http, "\r\n");
    if (!p) { error(c, "Invalid HTTP header"); return -1; }
    p += 2;
    while (p < end) {
        const char *e = strstr(p, "\r\n");
        if (!e || e > end) break;
        size_t len = (size_t)(e - p);
        if (len > 10 && !strncasecmp(p, "Location:", 9)) {
            const char *v = p + 9;
            while (v < e && isspace((unsigned char)*v)) v++;
            if ((size_t)(e-v) < sizeof(location)) { memcpy(location,v,(size_t)(e-v)); location[e-v]=0; }
        } else if (len > 21 && !strncasecmp(p,"Sec-WebSocket-Accept:",21)) {
            const char *v=p+21; while(v<e && isspace((unsigned char)*v))v++;
            if((size_t)(e-v)<sizeof(accept)){memcpy(accept,v,(size_t)(e-v));accept[e-v]=0;}
        } else if (len > 8 && !strncasecmp(p,"Upgrade:",8)) {
            upgrade = true;
        } else if (len > 11 && !strncasecmp(p,"Connection:",11)) {
            connection = true;
        }
        p=e+2;
    }
    if (status == 307 && location[0]) {
        if (!redirect(c, location)) { error(c, "WebSocket HTTP 307 redirect rejected"); return -1; }
        return 2;
    }
    if (status != 101) {
        char why[96]; snprintf(why,sizeof(why),"WebSocket HTTP handshake rejected (%d)",status);
        error(c,why); return -1;
    }
    if (!upgrade || !connection) { error(c, "Incomplete WebSocket upgrade response"); return -1; }
    char concat[128];
    snprintf(concat,sizeof(concat),"%s%s",c->key,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)concat,strlen(concat),digest);
    char expected[32];
    EVP_EncodeBlock((unsigned char *)expected,digest,sizeof(digest));
    if (strcmp(accept,expected)) { error(c,"WebSocket accept signature mismatch"); return -1; }
    size_t header_bytes=(size_t)(end-c->http)+4;
    if (c->http_len>header_bytes) {
        size_t remain=c->http_len-header_bytes;
        memcpy(c->rx,c->http+header_bytes,remain);
        c->rx_len=remain;
        c->rx_off=0;
    }
    struct wslay_event_callbacks callbacks = {
        .recv_callback=ws_read, .send_callback=ws_write,
        .genmask_callback=ws_mask, .on_msg_recv_callback=ws_message
    };
    if (wslay_event_context_client_init(&c->ws,&callbacks,c) != 0) {
        error(c,"Cannot create WebSocket framing context"); return -1;
    }
    wslay_event_config_set_max_recv_msg_length(c->ws,RR_WSS_FRAME_MAX);
    c->stage=WEBSOCKET;
    rr_transport_emit(c->transport,RR_TRANSPORT_EVENT_OPEN,NULL,NULL,0);
    if (c->rx_len && !c->closing && wslay_event_recv(c->ws)<0) {
        error(c,"Invalid frames following HTTP upgrade");return -1;
    }
    return 1;
}
static int send_frame(void *ctx, const uint8_t *data, size_t len, int opcode) {
    native_wss_t *c=ctx;
    if (!c->ws || c->dead || c->closing || len > RR_WSS_FRAME_MAX ||
        c->tx_len-c->tx_offset + wslay_event_get_queued_msg_length(c->ws) + len > RR_WSS_TX_MAX)
        return RR_TRANSPORT_BACKPRESSURE;
    struct wslay_event_msg msg = {.opcode=(uint8_t)opcode,.msg=(const uint8_t *)data,.msg_length=len};
    return wslay_event_queue_msg(c->ws,&msg)==0 ? RR_TRANSPORT_OK : RR_TRANSPORT_IO_ERROR;
}
static int send_text(void *ctx,const uint8_t *data,size_t len) { return send_frame(ctx,data,len,WSLAY_TEXT_FRAME); }
static int send_binary(void *ctx,const uint8_t *data,size_t len) { return send_frame(ctx,data,len,WSLAY_BINARY_FRAME); }
static size_t queued(void *ctx) {
    native_wss_t *c=ctx;
    return c->tx_len-c->tx_offset+(c->ws ? wslay_event_get_queued_msg_length(c->ws) : 0);
}
static uint32_t capabilities(void *ctx) {
    native_wss_t *c=ctx;
    return RR_TRANSPORT_RELIABLE_MESSAGES|RR_TRANSPORT_BINARY_MESSAGES|
      (c->tls ? RR_TRANSPORT_TLS_ENCRYPTED|RR_TRANSPORT_TLS_AUTHENTICATED : 0);
}
static void close_transport(void *ctx) { ((native_wss_t *)ctx)->closing=true; }
static int native_poll(void *ctx,int timeout_ms) {
    native_wss_t *c=ctx;
    if (c->dead || c->closing) { c->dead=true; goto finish; }
    if(c->stage==RESOLVING) {
        if(!resolver_ready(c,timeout_ms)) {
            if(c->dead)goto finish;
            return 0;
        }
    }
    struct pollfd pfd={.fd=c->fd,.events=POLLIN|POLLOUT};
    if (c->fd < 0) { error(c,"WebSocket has no socket"); goto finish; }
    int ready=poll(&pfd,1,timeout_ms);
    if (ready<0 && errno != EINTR) { error(c,"WebSocket poll failed"); goto finish; }
    if (ready<=0 && c->stage!=WEBSOCKET) return 0;
    if (ready<=0 && !(c->rx_len>c->rx_off || (c->ssl && SSL_pending(c->ssl)>0))) return 0;
    if (pfd.revents & (POLLERR|POLLHUP|POLLNVAL)) {
        if (c->stage != WEBSOCKET) { error(c,"WebSocket socket disconnected"); goto finish; }
    }
    if (c->stage==CONNECTING && (pfd.revents & POLLOUT)) {
        int status=0; socklen_t size=sizeof(status);
        if (getsockopt(c->fd,SOL_SOCKET,SO_ERROR,&status,&size)<0 || status) { error(c,"WebSocket TCP connect failed");goto finish; }
        if (c->tls) {
            if (!setup_tls(c)) { error(c,"WebSocket TLS setup failed");goto finish; }
            c->stage=TLS_HANDSHAKE;
        } else if (!begin_upgrade(c)) { error(c,"WebSocket request failed");goto finish; }
    }
    if (c->stage==TLS_HANDSHAKE) {
        int n=SSL_do_handshake(c->ssl);
        if (n==1) {
            if (SSL_get_verify_result(c->ssl)!=X509_V_OK || !begin_upgrade(c)) {
                error(c,"WebSocket TLS peer verification/upgrade failed");goto finish;
            }
        } else {
            int e=SSL_get_error(c->ssl,n);
            if (e!=SSL_ERROR_WANT_READ && e!=SSL_ERROR_WANT_WRITE) { error(c,"WebSocket TLS handshake failed");goto finish; }
        }
    }
    if (c->stage==HTTP_SEND && flush_tx(c)<0) { error(c,"WebSocket HTTP request write failed");goto finish; }
    if (c->stage==HTTP_SEND && !c->tx_len) c->stage=HTTP_READ;
    if (c->stage==HTTP_READ && (pfd.revents&POLLIN)) {
        if (c->http_len>=RR_WSS_HTTP_MAX) { error(c,"WebSocket HTTP response too large");goto finish; }
        int n=read_bytes(c,c->http+c->http_len,RR_WSS_HTTP_MAX-c->http_len);
        if (n==-1) { error(c,"WebSocket HTTP response read failed");goto finish; }
        if (n>0) {
            c->http_len+=(size_t)n;
            c->http[c->http_len]=0;
            int result=inspect_response(c);
            if (result==2) return 1;
            if (result<0) goto finish;
        }
    }
    if (c->stage==WEBSOCKET) {
        if (c->tx_len && flush_tx(c)<0) { error(c,"WebSocket TCP send failed");goto finish; }
        if (c->ws && wslay_event_want_write(c->ws) && c->tx_len-c->tx_offset < RR_WSS_TX_MAX/2) {
            if (wslay_event_send(c->ws)<0) { error(c,"WebSocket framing send failed");goto finish; }
            if (flush_tx(c)<0) { error(c,"WebSocket TCP send failed");goto finish; }
        }
        if ((pfd.revents&POLLIN) || c->rx_len>c->rx_off || (c->ssl && SSL_pending(c->ssl)>0)) {
            if (wslay_event_recv(c->ws)<0) { error(c,"WebSocket receive failed");goto finish; }
        }
        if ((pfd.revents&(POLLERR|POLLHUP|POLLNVAL)) || c->closing || !wslay_event_get_read_enabled(c->ws))
            c->dead=true;
    }
finish:
    if (c->dead) {
        reset_socket(c);
        rr_transport_emit(c->transport,RR_TRANSPORT_EVENT_CLOSE,NULL,NULL,0);
    }
    return c->dead ? RR_TRANSPORT_CLOSED_ERROR : 1;
}
static const rr_transport_driver_t ops={
    .abi_version=RR_TRANSPORT_ABI_VERSION,
    .capabilities=RR_TRANSPORT_RELIABLE_MESSAGES|RR_TRANSPORT_BINARY_MESSAGES|
                  RR_TRANSPORT_TLS_ENCRYPTED|RR_TRANSPORT_TLS_AUTHENTICATED,
    .connection_capabilities=capabilities,
    .send_message=send_text,.send_binary_message=send_binary,
    .queued_bytes=queued,.poll=native_poll,.close=close_transport
};
rr_transport_t *rr_transport_wss_connect(const char *url,const rr_wss_client_options_t *options,
                                          rr_transport_callback_t cb,void *user) {
    if (!url) return NULL;
    native_wss_t *c=calloc(1,sizeof(*c));
    if (!c) return NULL;
    c->fd=-1;
    c->resolver_fd=-1;
    c->allow_cross_origin=options && options->allow_cross_origin_redirect;
    const char *ua=options && options->user_agent ? options->user_agent : "RustyRig/1.0";
    if (strlen(ua)>=sizeof(c->user_agent) || strpbrk(ua,"\r\n") || !parse_url(c,url)) {
        free(c);return NULL;
    }
    snprintf(c->user_agent,sizeof(c->user_agent),"%s",ua);
    if (!start(c)) { reset_socket(c); free(c); return NULL; }
    c->transport=rr_transport_new(&ops,c,cb,user);
    if (!c->transport) { reset_socket(c); free(c); return NULL; }
    return c->transport;
}
void rr_transport_wss_traffic(rr_transport_t *t, uint64_t *rx, uint64_t *tx) {
    native_wss_t *c=t ? rr_transport_driver_context(t) : NULL;
    if (rx) *rx=c ? c->rx_bytes : 0;
    if (tx) *tx=c ? c->tx_bytes : 0;
}
void rr_transport_wss_release(rr_transport_t *t) {
    if (!t || rr_transport_state(t)!=RR_TRANSPORT_CLOSED) return;
    native_wss_t *c=rr_transport_driver_context(t);
    reset_socket(c);
    free(c);
    rr_transport_free(t);
}
