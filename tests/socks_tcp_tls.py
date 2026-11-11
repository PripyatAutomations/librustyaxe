#!/usr/bin/env python3
"""Loopback SOCKS5h/TLS regression with valid CA, bad hostname, and auth."""
import pathlib
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

def recv_exact(conn, n):
    data = b""
    while len(data) < n:
        part = conn.recv(n - len(data))
        if not part:
            raise AssertionError("SOCKS or IRC socket closed prematurely")
        data += part
    return data

def serve(listener, ctx, mode, auth, errors, sni):
    try:
        client, _ = listener.accept()
        client.settimeout(6)
        with client:
            greeting = recv_exact(client, 3)
            assert greeting == (b"\x05\x01\x02" if auth else b"\x05\x01\x00"), greeting
            response = b"\x05\x02" if auth else b"\x05\x00"
            client.sendall(response[:1])
            time.sleep(.01)
            client.sendall(response[1:])
            if auth:
                assert recv_exact(client, 2) == b"\x01\x04"
                assert recv_exact(client, 4) == b"user"
                assert recv_exact(client, 1) == b"\x06"
                assert recv_exact(client, 6) == b"secret"
                client.sendall(b"\x01\x00")
            if mode == "rejected":
                client.sendall(b"\x05\x05\x00\x01\x7f\x00\x00\x01\x00\x00")
                return
            request = recv_exact(client, 4)
            assert request == b"\x05\x01\x00\x03", request
            name = recv_exact(client, recv_exact(client, 1)[0])
            assert name == b"target.invalid", name  # DNS must occur at proxy
            port = int.from_bytes(recv_exact(client, 2), "big")
            assert port == 6697, port
            reply = b"\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00"
            client.sendall(reply[:3])
            time.sleep(.01)
            client.sendall(reply[3:])
            try:
                with ctx.wrap_socket(client, server_side=True) as tls:
                    tls.settimeout(5)
                    stream = b""
                    while b"USER " not in stream or not stream.endswith(b"\r\n"):
                        stream += recv_exact(tls, 1)
                        assert len(stream) < 2048, stream
                    assert b"NICK nonick\r\n" in stream
                    tls.sendall(b":fixture 001 nonick :Welcome\r\n")
                    time.sleep(.05)
            except ssl.SSLError:
                if mode != "mismatch":
                    raise
    except Exception as exc:
        errors.append(exc)

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: socks_tcp_tls.py TEST_CLIENT")
    with tempfile.TemporaryDirectory() as temp:
        cert = pathlib.Path(temp) / "cert.pem"
        key = pathlib.Path(temp) / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048",
                        "-keyout", str(key), "-out", str(cert), "-nodes",
                        "-days", "1", "-subj", "/CN=target.invalid",
                        "-addext", "subjectAltName=DNS:target.invalid"],
                       check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=10)
        for mode, auth in [("ok", False), ("ok", True), ("mismatch", False),
                           ("rejected", False)]:
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(str(cert), str(key))
            seen_sni = []
            ctx.set_servername_callback(
                lambda sock, name, context: seen_sni.append(name))
            errors = []
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                listener.settimeout(8)
                port = listener.getsockname()[1]
                worker = threading.Thread(target=serve, args=(
                    listener, ctx, mode, auth, errors, seen_sni))
                worker.start()
                try:
                    subprocess.run([sys.argv[1], str(port), str(cert),
                                    "auth" if auth else "none", mode],
                                   check=True, timeout=12)
                finally:
                    worker.join(timeout=9)
                assert not worker.is_alive(), "SOCKS fixture hung"
                assert not errors, errors
                if mode != "rejected":
                    assert seen_sni == [("wrong.invalid" if mode == "mismatch"
                                        else "target.invalid")], seen_sni
        print("PASS: IRC TLS through SOCKS5h, auth, remote DNS, SNI and CA checks")

if __name__ == "__main__":
    main()
