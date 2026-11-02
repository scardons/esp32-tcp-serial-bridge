import { execFile } from 'node:child_process';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { BridgeClient } from '../support/bridge-client.ts';
import { recordMeasurement } from '../support/measurements.ts';

const enabled = process.env.RUN_H6 === 'true';
const suite = enabled ? describe : describe.skip;

const STALLED_CLIENT_TIMEOUT_MS = 100000;

// Opt-in: it floods the UART for up to 100 s and ends in a forced disconnect.
// Run it with: npm run test:h6
suite('H6: Stalled TCP client detection', () => {
  test('H6: the server disconnects a client that stops reading, then accepts a new one', async () => {
    const { stdout } = await promisify(execFile)(
      'python3',
      ['-I', resolve('tests/support/stalled_client.py')],
      {
        env: {
          ...process.env,
          H6_TIMEOUT_S: String(STALLED_CLIENT_TIMEOUT_MS / 1000),
        },
      },
    );
    const outcome: { disconnected: boolean; seconds: number; error: string } = JSON.parse(stdout);

    recordMeasurement('H6', 'Disconnected after', `${outcome.seconds.toFixed(2)} s`);
    recordMeasurement('H6', 'Client saw', outcome.error || 'no disconnection');
    expect(outcome.disconnected).toBe(true);

    const next = await BridgeClient.connectActive(10000);

    await next.close();
  }, STALLED_CLIENT_TIMEOUT_MS + 15000);
});
