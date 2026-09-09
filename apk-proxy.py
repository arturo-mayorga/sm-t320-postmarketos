#!/usr/bin/env python3
"""Minimal HTTP/HTTPS(CONNECT) forward proxy, for apk on the tablet via ssh -R."""
import socket, threading, select, sys
from urllib.parse import urlsplit

def pipe(a, b):
    try:
        while True:
            r, _, _ = select.select([a, b], [], [], 60)
            if not r: break
            for s in r:
                d = s.recv(65536)
                if not d: return
                (b if s is a else a).sendall(d)
    except OSError: pass

def handle(c):
    try:
        head = b''
        while b'\r\n\r\n' not in head:
            d = c.recv(65536)
            if not d: return
            head += d
        req, rest = head.split(b'\r\n\r\n', 1)
        line = req.split(b'\r\n')[0].decode(errors='replace')
        method, target, ver = line.split()
        if method == 'CONNECT':
            host, port = target.rsplit(':', 1)
            u = socket.create_connection((host, int(port)), timeout=20)
            c.sendall(b'HTTP/1.1 200 Connection established\r\n\r\n')
            if rest: u.sendall(rest)
        else:
            p = urlsplit(target)
            u = socket.create_connection((p.hostname, p.port or 80), timeout=20)
            path = (p.path or '/') + ('?' + p.query if p.query else '')
            hdrs = [h for h in req.split(b'\r\n')[1:] if not h.lower().startswith(b'proxy-')]
            u.sendall(f'{method} {path} {ver}\r\n'.encode() + b'\r\n'.join(hdrs) + b'\r\n\r\n' + rest)
        pipe(c, u); u.close()
    except Exception as e:
        sys.stderr.write(f'proxy: {e}\n')
    finally:
        c.close()

srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', 3128)); srv.listen(32)
print('proxy listening on 127.0.0.1:3128', flush=True)
while True:
    conn, _ = srv.accept()
    threading.Thread(target=handle, args=(conn,), daemon=True).start()
