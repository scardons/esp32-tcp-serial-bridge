/*
 * Operator-assisted H7 and H8. A person moves the cable and confirms the serial
 * log; this script only guides the steps and checks what TCP can see.
 *
 *   npm run manual:h7   Ethernet disconnection with a client connected
 *   npm run manual:h8   Same test with an outage longer than 2 minutes
 *   npm run manual:h9   FR-9: repeated outages, with timed recovery
 *
 * H9 settings: H9_CYCLES (or a second argument, default 5) and H9_OUTAGE_S
 * (default 10). Example: H9_OUTAGE_S=15 npm run manual:h9 -- 20
 */
import { execFile } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { stdin, stdout } from 'node:process';
import { createInterface } from 'node:readline/promises';
import { promisify } from 'node:util';
import { BridgeClient, sleep } from '../support/bridge-client.ts';
import { bridge, target } from '../support/config.ts';

type Check = { name: string; passed: boolean; detail: string };

const testId = process.argv[2];

if (testId !== 'h7' && testId !== 'h8' && testId !== 'h9') {
  console.error('Usage: node tests/manual/link-recovery.ts <h7|h8|h9> [h9 cycles]');
  process.exit(2);
}

const MINIMUM_OUTAGE_MS = testId === 'h8' ? 125000 : 10000;
const RECOVERY_TIMEOUT_MS = 30000;

const prompt = createInterface({ input: stdin, output: stdout });
const checks: Check[] = [];

function record(name: string, passed: boolean, detail: string): void {
  checks.push({ name, passed, detail });
  console.log(`  ${passed ? 'PASS' : 'FAIL'}  ${name}: ${detail}`);
}

async function ping(): Promise<boolean> {
  try {
    await promisify(execFile)('ping', ['-c', '2', '-W', '1', bridge.host]);
    return true;
  } catch {
    return false;
  }
}

async function echoWorks(client: BridgeClient, text: string): Promise<boolean> {
  try {
    await client.write(text);
    return (await client.readExactly(text.length, 3000)).toString() === text;
  } catch {
    return false;
  }
}

async function main(): Promise<void> {
  console.log(`\n${testId.toUpperCase()} operator-assisted test against ${target}\n`);
  console.log('Keep the ESP32 serial monitor open, and do not restart the board.\n');

  const firstClient = await BridgeClient.connectActive();

  record('Client connected before the outage', true, 'connection accepted');
  record('Echo before the outage', await echoWorks(firstClient, 'before-outage'), 'through the loopback jumper');

  await prompt.question('Disconnect the Ethernet cable at the ESP32, then press Enter. ');

  const disconnectedAt = Date.now();

  for (;;) {
    const remaining = MINIMUM_OUTAGE_MS - (Date.now() - disconnectedAt);

    if (remaining <= 0) {
      break;
    }

    console.log(`  Keep it disconnected for ${Math.ceil(remaining / 1000)} s more`);
    await sleep(Math.min(remaining, 10000));
  }

  await prompt.question('Reconnect the Ethernet cable, then press Enter. ');

  const outageSeconds = (Date.now() - disconnectedAt) / 1000;
  const deadline = Date.now() + RECOVERY_TIMEOUT_MS;
  let nextClient: BridgeClient | undefined;

  while (!nextClient && Date.now() < deadline) {
    try {
      nextClient = await BridgeClient.connectActive(3000);
    } catch {
      await sleep(1000);
    }
  }

  record('Outage length', true, `${outageSeconds.toFixed(1)} s`);
  record('Ping after recovery', await ping(), bridge.host);
  record('New client accepted after recovery', nextClient !== undefined, target);

  if (nextClient) {
    record('Echo after recovery', await echoWorks(nextClient, 'after-outage'), 'through the loopback jumper');
    await nextClient.close();
  }

  firstClient.destroy();

  console.log('\nCheck the serial monitor for this sequence, with no reset between the lines:');
  console.log('  W NETWORK: Ethernet link down');
  console.log('  BRIDGE: TCP client closed: ...');
  console.log('  I NETWORK: Ethernet link up: 100 Mbps, full duplex, ...');
  console.log('  I NETWORK: IPv4 ready: 192.168.1.200, ...\n');

  const answer = await prompt.question('Did the monitor show all of it, with no reset? (y/n) ');

  record('Serial log confirmed by the operator', answer.trim().toLowerCase() === 'y', `answered "${answer.trim()}"`);
  prompt.close();

  const passed = checks.every((check) => check.passed);
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  const path = `docs/test-reports/manual-${testId}-${stamp}.md`;

  mkdirSync('docs/test-reports', { recursive: true });
  writeFileSync(path, [
    `# Manual Hardware Test ${testId.toUpperCase()}`,
    '',
    '| Field | Value |',
    '|---|---|',
    '| Validation type | **Manual, operator-assisted** |',
    `| Result | **${passed ? 'PASS' : 'FAIL'}** |`,
    `| Finished | ${new Date().toISOString()} |`,
    `| Target | \`${target}\` |`,
    `| Firmware | ${process.env.BRIDGE_FIRMWARE ?? 'Not provided (set `BRIDGE_FIRMWARE`)'} |`,
    '',
    '| Check | Result | Detail |',
    '|---|---|---|',
    ...checks.map((check) => `| ${check.name} | ${check.passed ? 'PASS' : 'FAIL'} | ${check.detail} |`),
    '',
    'Attach the ESP32 serial log for this run to the hardware validation record.',
    '',
  ].join('\n'));

  console.log(`\n${testId.toUpperCase()} result: ${passed ? 'PASS' : 'FAIL'}. Report: ${path}`);
  process.exit(passed ? 0 : 1);
}

/*
 * H9 (FR-9): the bridge recovers from repeated Ethernet outages without a
 * reboot. FR-9 states no recovery time. The 10 s target is a project test
 * criterion: from the operator's Enter, just before the cable goes back in,
 * to a TCP client the bridge keeps open. The operator's delay counts against
 * the bridge, so the measured time can only be longer than the real one.
 */
const H9_RECOVERY_TARGET_MS = 10000;
const H9_GIVE_UP_MS = 30000;
const H9_ATTEMPT_TIMEOUT_MS = 1000;
const H9_RETRY_DELAY_MS = 200;

type Cycle = {
  number: number;
  echoBefore: boolean;
  outageSeconds: number;
  pingMs?: number;
  acceptMs?: number;
  echoAfter: boolean;
  lastError: string;
  passed: boolean;
};

function h9Setting(name: string, raw: string | undefined, fallback: number): number {
  if (raw === undefined || raw === '') {
    return fallback;
  }

  const value = Number(raw);

  if (!Number.isInteger(value) || value <= 0) {
    throw new Error(`${name} must be a positive integer, got "${raw}"`);
  }

  return value;
}

async function pingOnce(): Promise<boolean> {
  try {
    await promisify(execFile)('ping', ['-c', '1', '-W', '1', bridge.host]);
    return true;
  } catch {
    return false;
  }
}

// Returns the time from start to the first ping reply, or undefined.
async function timeFirstPing(start: number, deadline: number): Promise<number | undefined> {
  while (Date.now() < deadline) {
    if (await pingOnce()) {
      return Date.now() - start;
    }

    await sleep(H9_RETRY_DELAY_MS);
  }

  return undefined;
}

// Returns the first client the bridge keeps open, with the time it took.
async function timeFirstAccept(start: number, deadline: number,
                               errors: string[]): Promise<{ client: BridgeClient; ms: number } | undefined> {
  while (Date.now() < deadline) {
    try {
      const client = await BridgeClient.connectActive(H9_ATTEMPT_TIMEOUT_MS, H9_ATTEMPT_TIMEOUT_MS);
      return { client, ms: Date.now() - start };
    } catch (error) {
      errors.push((error as Error).message);
      await sleep(H9_RETRY_DELAY_MS);
    }
  }

  return undefined;
}

async function waitOutage(minimumMs: number, disconnectedAt: number): Promise<void> {
  for (;;) {
    const remaining = minimumMs - (Date.now() - disconnectedAt);

    if (remaining <= 0) {
      return;
    }

    console.log(`  Keep it disconnected for ${Math.ceil(remaining / 1000)} s more`);
    await sleep(Math.min(remaining, 10000));
  }
}

// Never throws: any failure is recorded in the returned cycle, so the run can continue.
async function runCycle(number: number, outageMs: number): Promise<Cycle> {
  console.log(`\nCycle ${number}`);

  const cycle: Cycle = {
    number,
    echoBefore: false,
    outageSeconds: 0,
    echoAfter: false,
    lastError: '',
    passed: false,
  };
  let before: BridgeClient;

  try {
    before = await BridgeClient.connectActive();
  } catch (error) {
    cycle.lastError = `No client before the outage: ${(error as Error).message}`;
    console.log(`  FAIL  ${cycle.lastError}`);
    return cycle;
  }

  let after: BridgeClient | undefined;

  try {
    cycle.echoBefore = await echoWorks(before, `h9-before-${number}`);

    console.log(`  Client connected, echo ${cycle.echoBefore ? 'works' : 'FAILED'}`);
    await prompt.question('  Disconnect the Ethernet cable at the ESP32, then press Enter. ');

    const disconnectedAt = Date.now();

    await waitOutage(outageMs, disconnectedAt);
    await prompt.question('  Timing starts at Enter: press Enter, then reconnect the cable immediately. ');

    const start = Date.now();
    const deadline = start + H9_GIVE_UP_MS;
    const errors: string[] = [];

    cycle.outageSeconds = (start - disconnectedAt) / 1000;

    const [pingMs, accepted] = await Promise.all([
      timeFirstPing(start, deadline),
      timeFirstAccept(start, deadline, errors),
    ]);

    after = accepted?.client;
    cycle.pingMs = pingMs;
    cycle.acceptMs = accepted?.ms;
    cycle.lastError = errors.at(-1) ?? '';
    cycle.echoAfter = after ? await echoWorks(after, `h9-after-${number}`) : false;
    cycle.passed = cycle.echoBefore && pingMs !== undefined && cycle.acceptMs !== undefined &&
      cycle.acceptMs <= H9_RECOVERY_TARGET_MS && cycle.echoAfter;

    console.log(`  ${cycle.passed ? 'PASS' : 'FAIL'}  outage ${cycle.outageSeconds.toFixed(1)} s, ` +
      `ping after ${seconds(pingMs)}, client accepted after ${seconds(cycle.acceptMs)}, ` +
      `echo ${cycle.echoAfter ? 'works' : 'FAILED'}`);
  } catch (error) {
    cycle.passed = false;
    cycle.lastError = `Cycle aborted: ${(error as Error).message}`;
    console.log(`  FAIL  ${cycle.lastError}`);
  } finally {
    await after?.close();
    // The ESP32 dropped this session at link down. The PC side never learned of it.
    before.destroy();
  }

  return cycle;
}

function seconds(ms: number | undefined): string {
  return ms === undefined ? 'never' : `${(ms / 1000).toFixed(1)} s`;
}

async function runH9(): Promise<void> {
  const cycleCount = h9Setting('H9_CYCLES', process.argv[3] ?? process.env.H9_CYCLES, 5);
  const outageSeconds = h9Setting('H9_OUTAGE_S', process.env.H9_OUTAGE_S, 10);

  console.log(`\nH9 operator-assisted test against ${target}`);
  console.log(`${cycleCount} cycles, outages of at least ${outageSeconds} s. Project criterion: a TCP ` +
    `client accepted within ${H9_RECOVERY_TARGET_MS / 1000} s of reconnection.\n`);
  console.log('Keep the ESP32 serial monitor open (ideally with --timestamps), and do not restart the board.');

  const cycles: Cycle[] = [];

  for (let number = 1; number <= cycleCount; number++) {
    cycles.push(await runCycle(number, outageSeconds * 1000));
  }

  console.log('\nCheck the serial monitor. For each cycle n, with no reset anywhere:');
  console.log('  W NETWORK: Ethernet link down (#n)');
  console.log('  BRIDGE: TCP client closed: ...');
  console.log('  I NETWORK: Ethernet link up: 100 Mbps, full duplex, ...');
  console.log('  I NETWORK: Ethernet link restored after <x.x> s outage (#n)');
  console.log('  I NETWORK: IPv4 ready: 192.168.1.200, ...');
  console.log('  I NETWORK: Static IP restored after link recovery (#n)');

  console.log('A lost-IP line is not a failure on its own, as long as it says the static IP was kept:');
  console.log('  W NETWORK: ESP-NETIF lost-IP event during outage #n (...). Static IP 192.168.1.200 kept ...');

  if (outageSeconds > 120) {
    console.log('  It must appear once in every outage, since each one lasts over 120 s.');
  } else {
    console.log('  It may appear in a short outage: the 120 s timer can start at an earlier link down.');
  }

  console.log('Failures, even if every cycle passed over TCP:');
  console.log('  "IPv4 does not match the static configuration"');
  console.log('  "static configuration was not kept"');
  console.log('  any outage without "Static IP restored after link recovery (#n)"\n');

  let answer: string;

  try {
    answer = (await prompt.question('Did the monitor show all of it, with no reset? (y/n) ')).trim();
  } catch (error) {
    answer = `no answer: ${(error as Error).message}`;
  } finally {
    prompt.close();
  }

  const logConfirmed = answer.toLowerCase() === 'y';

  const accepts = cycles.flatMap((cycle) => (cycle.acceptMs === undefined ? [] : [cycle.acceptMs]));
  const passedCycles = cycles.filter((cycle) => cycle.passed).length;
  const passed = passedCycles === cycles.length && logConfirmed;
  const acceptSummary = accepts.length === 0 ? 'no client accepted' :
    `min ${seconds(Math.min(...accepts))}, mean ` +
    `${seconds(accepts.reduce((sum, ms) => sum + ms, 0) / accepts.length)}, max ${seconds(Math.max(...accepts))}`;
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  const path = `docs/test-reports/manual-h9-${stamp}.md`;

  mkdirSync('docs/test-reports', { recursive: true });
  writeFileSync(path, [
    '# Manual Hardware Test H9: Ethernet Link Recovery (FR-9)',
    '',
    '| Field | Value |',
    '|---|---|',
    '| Validation type | **Manual, operator-assisted** |',
    `| Result | **${passed ? 'PASS' : 'FAIL'}** |`,
    `| Finished | ${new Date().toISOString()} |`,
    `| Target | \`${target}\` |`,
    `| Firmware | ${process.env.BRIDGE_FIRMWARE ?? 'Not provided (set `BRIDGE_FIRMWARE`)'} |`,
    `| Cycles passed | ${passedCycles} of ${cycles.length} |`,
    `| Time to accepted client | ${acceptSummary} |`,
    `| Serial log confirmed by the operator | ${logConfirmed ? 'Yes' : 'No'} (answered "${answer.trim()}") |`,
    '',
    `**Project test criterion:** a TCP client is accepted within ${H9_RECOVERY_TARGET_MS / 1000} s of ` +
      'cable reconnection. FR-9 itself states no recovery time. Timing starts when the operator ' +
      'presses Enter, just before reconnecting the cable, so the operator\'s delay is included. ' +
      '"Accepted" means the bridge kept the connection open for 250 ms.',
    '',
    '| Cycle | Echo before | Outage | First ping reply | Client accepted | Echo after | Last connection error | Result |',
    '|---|---|---|---|---|---|---|---|',
    ...cycles.map((cycle) => `| ${cycle.number} | ${cycle.echoBefore ? 'PASS' : 'FAIL'} | ` +
      `${cycle.outageSeconds.toFixed(1)} s | ${seconds(cycle.pingMs)} | ${seconds(cycle.acceptMs)} | ` +
      `${cycle.echoAfter ? 'PASS' : 'FAIL'} | ${cycle.lastError.replace(/\|/g, '/') || 'None'} | ` +
      `${cycle.passed ? 'PASS' : 'FAIL'} |`),
    '',
    'Attach the ESP32 serial log for this run to the FR-9 hardware validation record.',
    '',
  ].join('\n'));

  console.log(`\nH9 result: ${passed ? 'PASS' : 'FAIL'}. ${passedCycles} of ${cycles.length} cycles passed. ` +
    `Report: ${path}`);
  process.exit(passed ? 0 : 1);
}

(testId === 'h9' ? runH9() : main()).catch((error: Error) => {
  console.error(`\n${testId.toUpperCase()} result: FAIL. ${error.message}`);
  process.exit(1);
});
