import { BridgeClient, sleep } from '../support/bridge-client.ts';
import { recordMeasurement } from '../support/measurements.ts';

const PREVIOUS_SESSION_BYTE = 0xcd;
const OBSERVATION_MS = 1500;

describe('H5: Session isolation', () => {
  test('H5: data from a closed session never reaches the next client', async () => {
    const previous = await BridgeClient.connectActive();

    await previous.write(Buffer.alloc(2000, PREVIOUS_SESSION_BYTE));
    await sleep(50);
    previous.destroy();

    const next = await BridgeClient.connectActive();

    try {
      await sleep(OBSERVATION_MS);

      recordMeasurement('H5', 'Bytes received by the next client', next.receivedBytes);
      expect(next.receivedBytes).toBe(0);

      // Silence only counts if the new session really works.
      await next.write('fresh-session');
      const echo = await next.readExactly(13, 3000);

      expect(echo.toString()).toBe('fresh-session');
    } finally {
      await next.close();
    }
  });
});
