import { appendFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';

export const MEASUREMENTS_PATH = resolve('test-results/measurements.jsonl');

/** Stores a value for the report. Jest does not forward console output to reporters. */
export function recordMeasurement(test: string, name: string, value: string | number): void {
  mkdirSync(dirname(MEASUREMENTS_PATH), { recursive: true });
  appendFileSync(MEASUREMENTS_PATH, `${JSON.stringify({ test, name, value })}\n`);
}
