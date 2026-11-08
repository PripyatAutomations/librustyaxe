/* SPDX-License-Identifier: MIT
 * Loopback native WebSocket regression: HTTP/101 + first binary frame in one
 * write, SHA-1 accept verification, and queued client messages.
 */
#define _POSIX_C_SOURCE 200809L
#include <librustyaxe/transport.wss.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

struct state { unsigned open, binary, closed, errors; };
static void receive_message(rr_transport_t *transport, enum rr_transport_event ev,
                            rr_transport_channel_t *channel, const uint8_t *data,
                            size_t len, void *context) {
    (void)channel;
    struct state *state=context;
    if (ev==RR_TRANSPORT_EVENT_OPEN) {
        state->open++;
        assert(rr_transport_send_message(transport,"hello",5)==RR_TRANSPORT_OK);
    } else if (ev==RR_TRANSPORT_EVENT_BINARY_MESSAGE) {
        assert(len==5 && !memcmp(data,"first",5));
        state->binary++;
    } else if (ev==RR_TRANSPORT_EVENT_MESSAGE) {
        assert(len==2 && !memcmp(data,"ok",2));
        rr_transport_close(transport);
    } else if (ev==RR_TRANSPORT_EVENT_ERROR) {
        fprintf(stderr,"native ws error: %.*s\n",(int)len,(const char *)data);
        state->errors++;
    } else if (ev==RR_TRANSPORT_EVENT_CLOSE) {
        state->closed++;
    }
}
static void child_serve(int listenfd) {
    struct pollfd waitfd={.fd=listenfd,.events=POLLIN};
    if (poll(&waitfd,1,5000)!=1) _exit(3);
    int fd=accept(listenfd,NULL,NULL);
    if (fd<0) _exit(4);
    char req[4096];
    size_t length=0;
    while (length<sizeof(req)-1) {
        struct pollfd pfd={.fd=fd,.events=POLLIN};
        if (poll(&pfd,1,5000)!=1) _exit(5);
        int n=recv(fd,req+length,sizeof(req)-length-1,0);
        if(n<=0) _exit(6);
        length+=(size_t)n;
        req[length]=0;
        if(strstr(req,"\r\n\r\n"))break;
    }
    assert(strstr(req, "Sec-WebSocket-Protocol: rustyrig.v2\\r\\n"));
    const char *key=strstr(req,"Sec-WebSocket-Key:");
    if(!key) _exit(7);
    key+=strlen("Sec-WebSocket-Key:");
    while (*key==' ')key++;
    const char *end=strstr(key,"\r\n");
    if(!end || end-key>30) _exit(8);
    char keybuf[96];
    snprintf(keybuf,sizeof(keybuf),"%.*s%s",(int)(end-key),key,
             "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)keybuf,strlen(keybuf),digest);
    unsigned char encoded[40];
    EVP_EncodeBlock(encoded,digest,sizeof(digest));
    char reply[512];
    int replylen=snprintf(reply,sizeof(reply),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",encoded);
    if(replylen<0 || (size_t)replylen+7>sizeof(reply)) _exit(9);
    /* The native client must not discard a frame attached to HTTP upgrade. */
    const unsigned char packet[]={0x82,0x05,'f','i','r','s','t'};
    memcpy(reply+replylen,packet,sizeof(packet));
    if(send(fd,reply,(size_t)replylen+sizeof(packet),0)!=(ssize_t)((size_t)replylen+sizeof(packet))) _exit(10);
    /* Confirm outbound client frames are RFC6455-masked and unmask correctly. */
    unsigned char frame[11];
    size_t got=0;
    while(got<sizeof(frame)) {
        struct pollfd incoming={.fd=fd,.events=POLLIN};
        if(poll(&incoming,1,5000)!=1)_exit(11);
        int n=recv(fd,frame+got,sizeof(frame)-got,0);
        if(n<=0)_exit(12);
        got+=(size_t)n;
    }
    if(frame[0]!=0x81 || frame[1]!=0x85)_exit(13);
    for(unsigned i=0;i<5;i++) {
        if((frame[6+i]^frame[2+i%4])!=(unsigned char)"hello"[i])_exit(14);
    }
    const unsigned char ack[]={0x81,0x02,'o','k'};
    if(send(fd,ack,sizeof(ack),0)!=(ssize_t)sizeof(ack))_exit(15);
    /* Give the client time to receive the ACK and start closing. */
    struct pollfd closing={.fd=fd,.events=POLLIN};
    poll(&closing,1,1000);
    close(fd);
    close(listenfd);
    _exit(0);
}
int main(void) {
    signal(SIGPIPE,SIG_IGN);
    int fd=socket(AF_INET,SOCK_STREAM,0);
    assert(fd>=0);
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK),.sin_port=0};
    assert(bind(fd,(void *)&addr,sizeof(addr))==0);
    assert(listen(fd,1)==0);
    socklen_t len=sizeof(addr);
    assert(getsockname(fd,(void *)&addr,&len)==0);
    pid_t pid=fork();
    assert(pid>=0);
    if(pid==0) child_serve(fd);
    close(fd);
    char url[100];
    snprintf(url,sizeof(url),"ws://127.0.0.1:%u/test",(unsigned)ntohs(addr.sin_port));
    struct state state={0};
    rr_wss_client_options_ex_t options = {.subprotocol = "rustyrig.v2"};
    rr_transport_t *t=rr_transport_wss_connect_ex(url,&options,receive_message,&state);
    assert(t!=NULL);
    for(unsigned i=0;i<300 && !state.closed;i++) {
        rr_transport_poll(t,10);
        if(state.errors)break;
    }
    fprintf(stderr,"native ws states: open=%u binary=%u errors=%u closed=%u last-state=%d\n",
        state.open,state.binary,state.errors,state.closed,(int)rr_transport_state(t));
    assert(state.open==1 && state.binary==1 && state.errors==0 && state.closed==1);
    assert(rr_transport_state(t)==RR_TRANSPORT_CLOSED);
    rr_transport_wss_release(t);
    int code=0;
    assert(waitpid(pid,&code,0)==pid && WIFEXITED(code) && WEXITSTATUS(code)==0);
    puts("PASS: native WebSocket 101+frame, text send, lifecycle");
    return 0;
}
