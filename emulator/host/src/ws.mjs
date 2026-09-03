/**
 * A minimal RFC 6455 WebSocket server, enough for JSON-RPC over localhost.
 *
 * The emulator has no dependencies on purpose: it has to start in under two
 * seconds from a cold checkout, and a dependency-free tree means there is
 * nothing to install first. A WebSocket server that handles text frames,
 * fragmentation, ping and close is about a hundred lines, so it is written out
 * here rather than pulled in.
 */

import { createHash, randomBytes } from "node:crypto";
import { EventEmitter } from "node:events";

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

const OPCODE = { CONTINUATION: 0x0, TEXT: 0x1, BINARY: 0x2, CLOSE: 0x8, PING: 0x9, PONG: 0xa };

export class WebSocketConnection extends EventEmitter {
  /**
   * @param {import("node:net").Socket} socket
   * @param {URL} url the request URL, which carries the caller identity
   */
  constructor(socket, url) {
    super();
    this.socket = socket;
    this.url = url;
    this.id = randomBytes(6).toString("hex");
    this.closed = false;

    this.buffer = Buffer.alloc(0);
    this.fragments = [];
    this.fragmentOpcode = null;

    socket.on("data", (chunk) => this.onData(chunk));
    socket.on("close", () => this.onClose());
    socket.on("error", (error) => {
      // EventEmitter treats an unhandled event literally named "error" as an
      // exception. A peer resetting a localhost socket is a normal disconnect,
      // so expose it under a non-fatal name and close cleanly.
      this.emit("socket-error", error);
      this.onClose();
    });
  }

  onData(chunk) {
    this.buffer = this.buffer.length === 0 ? chunk : Buffer.concat([this.buffer, chunk]);
    for (;;) {
      const frame = this.readFrame();
      if (!frame) return;
      this.handleFrame(frame);
      if (this.closed) return;
    }
  }

  /** Pull one complete frame out of the buffer, or return null if more is needed. */
  readFrame() {
    const buffer = this.buffer;
    if (buffer.length < 2) return null;

    const first = buffer[0];
    const second = buffer[1];
    const fin = (first & 0x80) !== 0;
    const opcode = first & 0x0f;
    const masked = (second & 0x80) !== 0;
    let length = second & 0x7f;
    let offset = 2;

    if (length === 126) {
      if (buffer.length < offset + 2) return null;
      length = buffer.readUInt16BE(offset);
      offset += 2;
    } else if (length === 127) {
      if (buffer.length < offset + 8) return null;
      const big = buffer.readBigUInt64BE(offset);
      // A JSON-RPC message that large is a bug or an attack; refuse it.
      if (big > 8n * 1024n * 1024n) {
        this.close(1009, "message too large");
        return null;
      }
      length = Number(big);
      offset += 8;
    }

    let mask = null;
    if (masked) {
      if (buffer.length < offset + 4) return null;
      mask = buffer.subarray(offset, offset + 4);
      offset += 4;
    }

    if (buffer.length < offset + length) return null;
    const payload = Buffer.from(buffer.subarray(offset, offset + length));
    if (mask) {
      for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];
    }

    this.buffer = buffer.subarray(offset + length);
    return { fin, opcode, payload };
  }

  handleFrame(frame) {
    switch (frame.opcode) {
      case OPCODE.PING:
        this.sendFrame(OPCODE.PONG, frame.payload);
        return;
      case OPCODE.PONG:
        return;
      case OPCODE.CLOSE:
        this.close(1000, "");
        return;
      case OPCODE.CONTINUATION:
        this.fragments.push(frame.payload);
        break;
      case OPCODE.TEXT:
      case OPCODE.BINARY:
        this.fragments = [frame.payload];
        this.fragmentOpcode = frame.opcode;
        break;
      default:
        this.close(1002, "unsupported opcode");
        return;
    }

    if (!frame.fin) return;
    const message = Buffer.concat(this.fragments);
    this.fragments = [];
    if (this.fragmentOpcode === OPCODE.TEXT) this.emit("message", message.toString("utf8"));
    else this.emit("binary", message);
  }

  sendFrame(opcode, payload) {
    if (this.closed || this.socket.destroyed) return;
    const length = payload.length;
    let header;
    if (length < 126) {
      header = Buffer.alloc(2);
      header[1] = length;
    } else if (length < 65536) {
      header = Buffer.alloc(4);
      header[1] = 126;
      header.writeUInt16BE(length, 2);
    } else {
      header = Buffer.alloc(10);
      header[1] = 127;
      header.writeBigUInt64BE(BigInt(length), 2);
    }
    header[0] = 0x80 | opcode; // always a single, final frame from the server
    this.socket.write(Buffer.concat([header, payload]));
  }

  send(text) {
    this.sendFrame(OPCODE.TEXT, Buffer.from(text, "utf8"));
  }

  sendJson(value) {
    this.send(JSON.stringify(value));
  }

  close(code = 1000, reason = "") {
    if (this.closed) return;
    const payload = Buffer.alloc(2 + Buffer.byteLength(reason));
    payload.writeUInt16BE(code, 0);
    payload.write(reason, 2);
    this.sendFrame(OPCODE.CLOSE, payload);
    this.closed = true;
    this.socket.end();
    this.emit("close");
  }

  onClose() {
    if (this.closed) return;
    this.closed = true;
    this.emit("close");
  }
}

/**
 * Attach a WebSocket handler to an existing HTTP server.
 * @param {import("node:http").Server} server
 * @param {(connection: WebSocketConnection) => void} onConnection
 */
export function attachWebSocket(server, onConnection) {
  server.on("upgrade", (request, socket) => {
    const key = request.headers["sec-websocket-key"];
    if (request.headers.upgrade?.toLowerCase() !== "websocket" || !key) {
      socket.end("HTTP/1.1 400 Bad Request\r\n\r\n");
      return;
    }

    const accept = createHash("sha1").update(key + GUID).digest("base64");
    socket.write(
      "HTTP/1.1 101 Switching Protocols\r\n" +
        "Upgrade: websocket\r\n" +
        "Connection: Upgrade\r\n" +
        `Sec-WebSocket-Accept: ${accept}\r\n\r\n`,
    );
    socket.setNoDelay(true);

    const url = new URL(request.url ?? "/", "http://127.0.0.1");
    onConnection(new WebSocketConnection(socket, url));
  });
}
