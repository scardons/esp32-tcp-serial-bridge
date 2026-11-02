import { BridgeClient } from '../support/bridge-client.ts';
import { recordMeasurement } from '../support/measurements.ts';

const REJECTION_TIMEOUT_MS = 2000;

describe('H1: TCP connection and single-client restriction', () => {
  const clients: BridgeClient[] = [];

  async function open(connect: () => Promise<BridgeClient>): Promise<BridgeClient> {
    const client = await connect();

    clients.push(client);
    return client;
  }

  afterEach(async () => {
    await Promise.all(clients.splice(0).map((client) => client.close()));
  });

  test('H1: accepts a TCP client on the configured port', async () => {
    const client = await open(() => BridgeClient.connectActive());

    expect(client.isClosed).toBe(false);
  });

  test('H1: rejects a second client while one is active, and keeps the first working', async () => {
    const clientA = await open(() => BridgeClient.connectActive());
    const clientB = await open(() => BridgeClient.connect());

    const rejected = await clientB.waitForClose(REJECTION_TIMEOUT_MS);

    expect(rejected).toBe(true);
    expect(clientB.receivedBytes).toBe(0);
    expect(clientA.isClosed).toBe(false);

    await clientA.write('A-still-active');
    const echo = await clientA.readExactly(14, 3000);

    expect(echo.toString()).toBe('A-still-active');
  });

  test('H1: releases the slot when the client disconnects, without an ESP32 restart', async () => {
    const clientA = await open(() => BridgeClient.connectActive());

    await clientA.close();

    const started = Date.now();
    const clientB = await open(() => BridgeClient.connectActive());

    recordMeasurement('H1', 'Time to reconnect', `${Date.now() - started} ms`);
    expect(clientB.isClosed).toBe(false);
  });
});
