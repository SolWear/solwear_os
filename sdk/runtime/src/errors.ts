/** Error thrown when a call reaches the shell but is refused. */
export class SolwearRpcError extends Error {
  readonly code: number;
  readonly data: unknown;
  readonly method: string;

  constructor(method: string, code: number, message: string, data?: unknown) {
    super(`${method} failed (${code}): ${message}`);
    this.name = "SolwearRpcError";
    this.code = code;
    this.data = data;
    this.method = method;
  }
}

/** Error thrown when the SDK cannot reach a shell at all. */
export class SolwearBridgeError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "SolwearBridgeError";
  }
}
