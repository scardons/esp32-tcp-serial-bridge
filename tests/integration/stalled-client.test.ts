import { execFile } from 'node:child_process';
import { resolve } from 'node:path';
import { promisify } from 'node:util';
import { BridgeClient } from '../support/bridge-client.ts';
import { BLOCKED_PREFIX } from '../support/config.ts';
import { recordMeasurement } from '../support/measurements.ts';

const enabled = process.env.RUN_H6 === 'true';
const suite = enabled ? describe : describe.skip;

const STALLED_CLIENT_TIMEOUT_MS = 100000;
const STALLED_CLIENT = resolve('tests/support/stalled_client.py');

/** What stalled_client.py saw about the network path. */
interface PathDiagnostic {
  local: string;
  remote: string;
  mss: number | null;
  expected_mss: number;
  route: string;
  problems: string[];
}

interface StalledClientOutcome {
  blocked: boolean;
  disconnected: boolean;
  seconds: number;
  error: string;
  path?: PathDiagnostic;
}

/**
 * Returns why the run cannot judge the firmware, or null if it can. A proxy in
 * the path, such as VirtualBox NAT, can hide the server's reset from the
 * client, so an unvalidated path gives neither PASS nor FAIL.
 */
function blockedReason(outcome: StalledClientOutcome): string | null {
  if (!outcome.path) {
    return `${BLOCKED_PREFIX} H6 could not validate the network path: the stalled client reported no path check.`;
  }

  if (outcome.blocked || outcome.path.problems.length > 0) {
    const problems = outcome.path.problems.length > 0 ? outcome.path.problems.join('; ') : 'no reason given';

    return `${BLOCKED_PREFIX} H6 could not validate a direct path to the ESP32: ${problems}.`;
  }

  return null;
}

function recordPath(path: PathDiagnostic | undefined): void {
  if (!path) {
    return;
  }

  recordMeasurement('H6', 'Local address', path.local);
  recordMeasurement('H6', 'Remote address', path.remote);
  recordMeasurement('H6', 'Route to ESP32', path.route);
  recordMeasurement('H6', 'TCP MSS (diagnostic)', `${path.mss ?? 'unknown'}, expected ${path.expected_mss}`);
  recordMeasurement('H6', 'Path check',
    path.problems.length > 0 ? 'not validated' : 'consistent with a direct link (not proof)');
}

// Opt-in: it floods the UART for up to 100 s and ends in a forced disconnect.
// Run it with: npm run test:h6
suite('H6: Stalled TCP client detection', () => {
  describe('path check', () => {
    const direct: StalledClientOutcome = {
      blocked: false,
      disconnected: true,
      seconds: 7.1,
      error: 'ConnectionResetError',
      path: {
        local: '192.168.1.58:40000',
        remote: '192.168.1.200:4001',
        mss: 1440,
        expected_mss: 1440,
        route: 'on-link dev enp0s3',
        problems: [],
      },
    };

    test('H6 path check: the stalled client self-test passes', async () => {
      await expect(promisify(execFile)('python3', ['-I', STALLED_CLIENT, '--self-test']))
        .resolves.toBeDefined();
    });

    test('H6 path check: a direct path lets H6 judge the firmware', () => {
      expect(blockedReason(direct)).toBeNull();
    });

    test('H6 path check: a NAT path is BLOCKED, not FAIL', () => {
      const reason = blockedReason({
        ...direct,
        blocked: true,
        disconnected: false,
        path: {
          ...direct.path!,
          local: '10.0.2.15:58512',
          mss: 1460,
          route: 'via 10.0.2.2 dev enp0s3',
          problems: ['the ESP32 is reached through gateway 10.0.2.2', 'TCP MSS is 1460'],
        },
      });

      expect(reason).toMatch(new RegExp(`^${BLOCKED_PREFIX} `));
      expect(reason).toContain('gateway 10.0.2.2; TCP MSS is 1460');
    });

    test('H6 path check: a missing or inconsistent path check is BLOCKED', () => {
      expect(blockedReason({ ...direct, path: undefined })).toMatch(new RegExp(`^${BLOCKED_PREFIX} `));
      expect(blockedReason({ ...direct, blocked: true })).toMatch(new RegExp(`^${BLOCKED_PREFIX} `));
    });
  });

  test('H6: the server disconnects a client that stops reading, then accepts a new one', async () => {
    const { stdout } = await promisify(execFile)(
      'python3',
      ['-I', STALLED_CLIENT],
      {
        env: {
          ...process.env,
          H6_TIMEOUT_S: String(STALLED_CLIENT_TIMEOUT_MS / 1000),
        },
      },
    );
    const outcome: StalledClientOutcome = JSON.parse(stdout);

    recordPath(outcome.path);

    const blocked = blockedReason(outcome);

    if (blocked) {
      throw new Error(blocked);
    }

    recordMeasurement('H6', 'Disconnected after', `${outcome.seconds.toFixed(2)} s`);
    recordMeasurement('H6', 'Client saw', outcome.error || 'no disconnection');
    expect(outcome.disconnected).toBe(true);

    const next = await BridgeClient.connectActive(10000);

    await next.close();
  }, STALLED_CLIENT_TIMEOUT_MS + 15000);
});
