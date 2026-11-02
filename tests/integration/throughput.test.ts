import { BridgeClient } from '../support/bridge-client.ts';
import { BAUD_BYTES_PER_SECOND } from '../support/config.ts';
import { expectIdentical, pseudoRandomBytes, sha256 } from '../support/data.ts';
import { recordMeasurement } from '../support/measurements.ts';

const LENGTH = 256000;
const PAYLOAD = pseudoRandomBytes(LENGTH);

describe('H4: Large data transfer integrity', () => {
  test('H4: returns 256,000 bytes unchanged and in order', async () => {
    const client = await BridgeClient.connectActive();

    try {
      const started = Date.now();

      await client.write(PAYLOAD);

      const echo = await client.readExactly(LENGTH, 40000);
      const seconds = (Date.now() - started) / 1000;

      expectIdentical(echo, PAYLOAD);
      recordMeasurement('H4', 'Bytes returned', echo.length);
      recordMeasurement('H4', 'SHA256 sent', sha256(PAYLOAD));
      recordMeasurement('H4', 'SHA256 received', sha256(echo));
      recordMeasurement('H4', 'Transfer time', `${seconds.toFixed(2)} s`);
      recordMeasurement('H4', 'Throughput', `${Math.round(LENGTH / seconds)} bytes/s`);

      expect(seconds).toBeGreaterThan((LENGTH / BAUD_BYTES_PER_SECOND) * 0.9);
    } finally {
      await client.close();
    }
  }, 45000);
});
