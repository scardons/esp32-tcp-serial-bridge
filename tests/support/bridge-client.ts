import net from 'node:net';
import { bridge, target } from './config.ts';

/** A rejected client is closed by the server right after the TCP handshake. */
const REJECTION_WINDOW_MS = 250;

export function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export class BridgeClient {
  private readonly chunks: Buffer[] = [];
  private receivedLength = 0;
  private closed = false;
  private readonly closeWaiters: Array<() => void> = [];
  private readonly dataWaiters: Array<() => void> = [];

  private readonly socket: net.Socket;

  private constructor(socket: net.Socket) {
    this.socket = socket;

    socket.on('data', (chunk: Buffer) => {
      this.chunks.push(chunk);
      this.receivedLength += chunk.length;
      this.dataWaiters.splice(0).forEach((wake) => wake());
    });

    socket.on('close', () => {
      this.closed = true;
      this.closeWaiters.splice(0).forEach((wake) => wake());
      this.dataWaiters.splice(0).forEach((wake) => wake());
    });

    // Errors always end in 'close', which is what the tests observe.
    socket.on('error', () => {});
  }

  /** Opens a TCP connection. It does not prove that the bridge accepted the client. */
  static connect(attemptTimeoutMs = bridge.connectTimeoutMs): Promise<BridgeClient> {
    return new Promise((resolve, reject) => {
      const socket = net.createConnection({ host: bridge.host, port: bridge.port });
      const timer = setTimeout(() => {
        socket.destroy();
        reject(new Error(`ESP32 unavailable: no TCP connection to ${target} ` +
          `within ${attemptTimeoutMs} ms. Check power, cable and BRIDGE_HOST.`));
      }, attemptTimeoutMs);

      socket.once('error', (error: NodeJS.ErrnoException) => {
        clearTimeout(timer);
        reject(new Error(`ESP32 unavailable at ${target}: ${error.code ?? error.message}. ` +
          'Check power, cable, BRIDGE_HOST and that the firmware is running.'));
      });

      socket.once('connect', () => {
        clearTimeout(timer);
        socket.removeAllListeners('error');
        socket.setNoDelay(true);
        resolve(new BridgeClient(socket));
      });
    });
  }

  /** Connects and retries until the bridge keeps the connection open. */
  static async connectActive(timeoutMs = 5000,
                             attemptTimeoutMs = bridge.connectTimeoutMs): Promise<BridgeClient> {
    const deadline = Date.now() + timeoutMs;

    do {
      const client = await BridgeClient.connect(attemptTimeoutMs);

      if (!(await client.waitForClose(REJECTION_WINDOW_MS))) {
        return client;
      }

      await sleep(100);
    } while (Date.now() < deadline);

    throw new Error(`The bridge at ${target} rejected every connection for ${timeoutMs} ms. ` +
      'Another client is probably connected: close it and run the tests again.');
  }

  get isClosed(): boolean {
    return this.closed;
  }

  get receivedBytes(): number {
    return this.receivedLength;
  }

  received(): Buffer {
    return Buffer.concat(this.chunks, this.receivedLength);
  }

  write(data: Buffer | string): Promise<void> {
    return new Promise((resolve, reject) => {
      this.socket.write(data, (error) => (error ? reject(error) : resolve()));
    });
  }

  /** Resolves true if the socket closes within the given time. */
  waitForClose(timeoutMs: number): Promise<boolean> {
    if (this.closed) {
      return Promise.resolve(true);
    }

    return new Promise((resolve) => {
      const timer = setTimeout(() => resolve(false), timeoutMs);

      this.closeWaiters.push(() => {
        clearTimeout(timer);
        resolve(true);
      });
    });
  }

  /** Waits for a total number of received bytes, then returns them. */
  async readExactly(length: number, timeoutMs: number): Promise<Buffer> {
    const deadline = Date.now() + timeoutMs;

    while (this.receivedLength < length) {
      const remaining = deadline - Date.now();

      if (this.closed || remaining <= 0) {
        const reason = this.closed ? 'the connection closed' : `${timeoutMs} ms passed`;

        throw new Error(`Received ${this.receivedLength} of ${length} bytes before ${reason}. ` +
          'Check the GPIO32 to GPIO33 loopback jumper.');
      }

      await new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, remaining);

        this.dataWaiters.push(() => {
          clearTimeout(timer);
          resolve();
        });
      });
    }

    return this.received().subarray(0, length);
  }

  /** Closes normally, with a FIN. */
  async close(): Promise<void> {
    if (!this.closed) {
      this.socket.end();
      await this.waitForClose(1000);
    }

    this.socket.destroy();
  }

  destroy(): void {
    this.socket.destroy();
  }
}
