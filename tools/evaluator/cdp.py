# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
"""Minimal Chrome DevTools Protocol client (no third-party dependencies).

NW.js (and any Chromium) started with --remote-debugging-port=N serves a
target list at http://127.0.0.1:N/json and a WebSocket per page. This module
implements just enough of RFC 6455 (client side, text frames) to call CDP
methods synchronously.
"""

import base64
import json
import os
import socket
import struct
import time
import urllib.request
from urllib.parse import urlparse


class CdpError(Exception):
    pass


# --- WebSocket framing -------------------------------------------------------

def encode_frame(payload, opcode=0x1):
    """Encode one masked client frame (FIN set) carrying ``payload`` bytes."""
    if isinstance(payload, str):
        payload = payload.encode("utf-8")
    header = bytearray([0x80 | opcode])
    n = len(payload)
    if n < 126:
        header.append(0x80 | n)
    elif n < 0x10000:
        header.append(0x80 | 126)
        header += struct.pack(">H", n)
    else:
        header.append(0x80 | 127)
        header += struct.pack(">Q", n)
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
    return bytes(header) + mask + masked


def decode_frame(buf):
    """Decode one frame from the start of ``buf``.

    Returns (fin, opcode, payload, consumed) or None if ``buf`` does not
    hold a complete frame yet.
    """
    if len(buf) < 2:
        return None
    b0, b1 = buf[0], buf[1]
    fin = bool(b0 & 0x80)
    opcode = b0 & 0x0F
    masked = bool(b1 & 0x80)
    n = b1 & 0x7F
    pos = 2
    if n == 126:
        if len(buf) < 4:
            return None
        n = struct.unpack(">H", buf[2:4])[0]
        pos = 4
    elif n == 127:
        if len(buf) < 10:
            return None
        n = struct.unpack(">Q", buf[2:10])[0]
        pos = 10
    mask = None
    if masked:
        if len(buf) < pos + 4:
            return None
        mask = buf[pos:pos + 4]
        pos += 4
    if len(buf) < pos + n:
        return None
    payload = bytes(buf[pos:pos + n])
    if mask:
        payload = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
    return fin, opcode, payload, pos + n


class WebSocket:
    """Blocking WebSocket client for ws://host:port/path URLs."""

    def __init__(self, url, timeout=30.0):
        u = urlparse(url)
        self.sock = socket.create_connection((u.hostname, u.port), timeout=timeout)
        self.sock.settimeout(timeout)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        path = u.path or "/"
        if u.query:
            path += "?" + u.query
        request = (
            "GET {path} HTTP/1.1\r\n"
            "Host: {host}:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ).format(path=path, host=u.hostname, port=u.port, key=key)
        self.sock.sendall(request.encode("ascii"))
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise CdpError("WebSocket handshake failed: connection closed")
            response += chunk
        head, _, rest = response.partition(b"\r\n\r\n")
        status = head.split(b"\r\n", 1)[0]
        if b" 101 " not in status:
            raise CdpError("WebSocket handshake failed: %s" % status.decode("latin-1"))
        self.buf = bytearray(rest)
        self.fragments = []

    def send_text(self, text):
        self.sock.sendall(encode_frame(text, 0x1))

    def recv_text(self):
        """Return the next complete text message (handles fragmentation/ping)."""
        while True:
            frame = decode_frame(self.buf)
            if frame is None:
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise CdpError("WebSocket closed")
                self.buf += chunk
                continue
            fin, opcode, payload, consumed = frame
            del self.buf[:consumed]
            if opcode == 0x9:                       # ping -> pong
                self.sock.sendall(encode_frame(payload, 0xA))
                continue
            if opcode == 0x8:
                raise CdpError("WebSocket closed by peer")
            if opcode in (0x1, 0x0):
                self.fragments.append(payload)
                if fin:
                    message = b"".join(self.fragments)
                    self.fragments = []
                    return message.decode("utf-8")
            # binary/pong frames are ignored

    def close(self):
        try:
            self.sock.sendall(encode_frame(b"", 0x8))
        except OSError:
            pass
        self.sock.close()


# --- CDP session -------------------------------------------------------------

def list_targets(port, host="127.0.0.1"):
    with urllib.request.urlopen("http://%s:%d/json" % (host, port), timeout=5) as r:
        return json.loads(r.read().decode("utf-8"))


def wait_for_page(port, timeout=30.0, match="index.html"):
    """Poll the target list until a page whose URL contains ``match`` shows up."""
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            for target in list_targets(port):
                if target.get("type") == "page" and match in target.get("url", ""):
                    return target
        except (OSError, ValueError) as e:
            last_error = e
        time.sleep(0.25)
    raise CdpError("No page target on port %d after %.0fs (%s)" % (port, timeout, last_error))


class CdpSession:
    def __init__(self, ws_url, timeout=60.0):
        self.ws = WebSocket(ws_url, timeout=timeout)
        self.next_id = 1
        self.events = []

    def call(self, method, **params):
        msg_id = self.next_id
        self.next_id += 1
        self.ws.send_text(json.dumps({"id": msg_id, "method": method, "params": params}))
        while True:
            msg = json.loads(self.ws.recv_text())
            if msg.get("id") == msg_id:
                if "error" in msg:
                    raise CdpError("%s: %s" % (method, msg["error"].get("message")))
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def evaluate(self, expression, await_promise=True):
        """Evaluate JS in the page and return its JSON value.

        Throws CdpError with the exception text when the script throws.
        """
        result = self.call("Runtime.evaluate", expression=expression,
                           awaitPromise=await_promise, returnByValue=True)
        details = result.get("exceptionDetails")
        if details:
            exc = details.get("exception") or {}
            text = exc.get("description") or details.get("text") or "JS exception"
            raise CdpError(text)
        return result.get("result", {}).get("value")

    def screenshot_png(self):
        result = self.call("Page.captureScreenshot", format="png")
        return base64.b64decode(result["data"])

    def close(self):
        self.ws.close()
