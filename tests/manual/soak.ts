/*
 * NFR-3 continuous run: keeps the bridge busy for SOAK_MINUTES (default 15).
 *
 *   BRIDGE_FIRMWARE=<ELF SHA256> npm run manual:soak
 *
 * Back-to-back sessions of about SOAK_SESSION_S seconds (default 30) each send
 * a different pseudo-random payload, sized to fill the session at the UART
 * rate, and check that the echo is identical. The firmware logs its free and
 * minimum-ever heap after every session, so the serial log holds one heap
 * sample per session. Keep the monitor (with --timestamps) and the GPIO32 to
 * GPIO33 jumper connected. No operator action is needed during the run.
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import { BridgeClient, sleep } from '../support/bridge-client.ts';
import { BAUD_BYTES_PER_SECOND, target } from '../support/config.ts';
import { pseudoRandomBytes, sha256 } from '../support/data.ts';

type Session = { number: number; started: string; bytes: number; seconds: number; passed: boolean; detail: string };

function setting(name: string, fallback: number): number {
  const raw = process.env[name];
  const value = raw === undefined || raw === '' ? fallback : Number(raw);

  if (!Number.isFinite(value) || value <= 0) {
    throw new Error(`${name} must be a positive number, got "${raw}"`);
  }

  return value;
}

async function runSession(number: number, sessionSeconds: number): Promise<Session> {
  const started = new Date().toISOString();
  // 90 % of the UART rate, so the echo finishes within the session.
  const length = Math.round(BAUD_BYTES_PER_SECOND * sessionSeconds * 0.9);
  const payload = pseudoRandomBytes(length, 0x2545f491 + number);
  const t0 = Date.now();
  let client: BridgeClient | undefined;

  try {
    client = await BridgeClient.connectActive();
    await client.write(payload);

    const echo = await client.readExactly(length, sessionSeconds * 2000);
    const passed = echo.equals(payload);

    return {
      number, started, bytes: length, seconds: (Date.now() - t0) / 1000, passed,
      detail: passed ? `SHA256 ${sha256(payload).slice(0, 16)}… matches` :
        `echo differs: sent ${sha256(payload).slice(0, 16)}…, received ${sha256(echo).slice(0, 16)}…`,
    };
  } catch (error) {
    return { number, started, bytes: length, seconds: (Date.now() - t0) / 1000, passed: false,
      detail: (error as Error).message.replace(/\|/g, '/') };
  } finally {
    await client?.close();
  }
}

async function main(): Promise<void> {
  const minutes = setting('SOAK_MINUTES', 15);
  const sessionSeconds = setting('SOAK_SESSION_S', 30);
  const startedAt = new Date();
  const deadline = Date.now() + minutes * 60000;
  const sessions: Session[] = [];

  console.log(`NFR-3 soak against ${target}: ${minutes} min of back-to-back ${sessionSeconds} s sessions`);

  while (Date.now() < deadline) {
    const session = await runSession(sessions.length + 1, sessionSeconds);

    sessions.push(session);
    console.log(`  ${session.passed ? 'PASS' : 'FAIL'}  session ${session.number}, ` +
      `${session.bytes} bytes in ${session.seconds.toFixed(1)} s: ${session.detail}`);
    // Lets the firmware close the session and log its heap before the next one.
    await sleep(500);
  }

  const finishedAt = new Date();
  const passed = sessions.length > 0 && sessions.every((session) => session.passed);
  const bytes = sessions.reduce((sum, session) => sum + session.bytes, 0);
  const stamp = startedAt.toISOString().replace(/[:.]/g, '-');
  const path = `docs/test-reports/soak-${stamp}.md`;

  mkdirSync('docs/test-reports', { recursive: true });
  writeFileSync(path, [
    '# NFR-3 Continuous Run (Soak)',
    '',
    '| Field | Value |',
    '|---|---|',
    `| Result over TCP | **${passed ? 'PASS' : 'FAIL'}** |`,
    `| Started | ${startedAt.toISOString()} |`,
    `| Finished | ${finishedAt.toISOString()} |`,
    `| Duration | ${((finishedAt.getTime() - startedAt.getTime()) / 60000).toFixed(1)} min |`,
    `| Target | \`${target}\` |`,
    `| Firmware | ${process.env.BRIDGE_FIRMWARE ?? 'Not provided (set `BRIDGE_FIRMWARE`)'} |`,
    `| Sessions | ${sessions.filter((session) => session.passed).length} of ${sessions.length} passed |`,
    `| Bytes echoed and verified | ${bytes} |`,
    '',
    'This report covers what TCP can see. The heap samples are in the serial log (`Heap after session` lines), and are evaluated in the NFR-3 record.',
    '',
    '| Session | Started | Bytes | Seconds | Result | Detail |',
    '|---|---|---|---|---|---|',
    ...sessions.map((session) => `| ${session.number} | ${session.started} | ${session.bytes} | ` +
      `${session.seconds.toFixed(1)} | ${session.passed ? 'PASS' : 'FAIL'} | ${session.detail} |`),
    '',
  ].join('\n'));

  console.log(`\nSoak result over TCP: ${passed ? 'PASS' : 'FAIL'}. ${sessions.length} sessions. Report: ${path}`);
  process.exit(passed ? 0 : 1);
}

main().catch((error: Error) => {
  console.error(`Soak result: FAIL. ${error.message}`);
  process.exit(1);
});
