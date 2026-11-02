/*
 * Operator-assisted H7 and H8. A person moves the cable and confirms the serial
 * log; this script only guides the steps and checks what TCP can see.
 *
 *   npm run manual:h7   Ethernet disconnection with a client connected
 *   npm run manual:h8   Same test with an outage longer than 2 minutes
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

if (testId !== 'h7' && testId !== 'h8') {
  console.error('Usage: node tests/manual/link-recovery.ts <h7|h8>');
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

main().catch((error: Error) => {
  console.error(`\n${testId.toUpperCase()} result: FAIL. ${error.message}`);
  process.exit(1);
});
