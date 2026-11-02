import { BridgeClient } from '../support/bridge-client.ts';
import { BAUD_BYTES_PER_SECOND } from '../support/config.ts';
import { allByteValues, expectIdentical, sha256 } from '../support/data.ts';
import { recordMeasurement } from '../support/measurements.ts';

const PAYLOAD = allByteValues(80);

describe('H3: Binary data integrity', () => {
  test('H3: returns 20,480 bytes covering every value from 0x00 to 0xFF, unchanged', async () => {
    expect(PAYLOAD.length).toBe(20480);

    const client = await BridgeClient.connectActive();

    try {
      const started = Date.now();

      await client.write(PAYLOAD);

      const echo = await client.readExactly(PAYLOAD.length, 10000);
      const seconds = (Date.now() - started) / 1000;

      expectIdentical(echo, PAYLOAD);
      recordMeasurement('H3', 'Bytes returned', echo.length);
      recordMeasurement('H3', 'SHA256 sent', sha256(PAYLOAD));
      recordMeasurement('H3', 'SHA256 received', sha256(echo));
      recordMeasurement('H3', 'Transfer time', `${seconds.toFixed(2)} s`);

      // Faster than the baud rate means the data did not go through the UART.
      expect(seconds).toBeGreaterThan((PAYLOAD.length / BAUD_BYTES_PER_SECOND) * 0.9);
    } finally {
      await client.close();
    }
  });
});
