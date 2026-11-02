import { createHash } from 'node:crypto';

/** The H3 payload: every byte value from 0x00 to 0xFF, repeated. */
export function allByteValues(repeat: number): Buffer {
  const pattern = Buffer.from(Array.from({ length: 256 }, (_, value) => value));

  return Buffer.concat(Array.from({ length: repeat }, () => pattern));
}

/** Deterministic and non-repeating, so a moved block cannot match by chance. */
export function pseudoRandomBytes(length: number, seed = 0x2545f491): Buffer {
  const data = Buffer.alloc(length);
  let state = seed;

  for (let index = 0; index < length; index++) {
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    state >>>= 0;
    data[index] = state & 0xff;
  }

  return data;
}

export function sha256(data: Buffer): string {
  return createHash('sha256').update(data).digest('hex');
}

/** Fails with the position of the first wrong byte, not a dump of both buffers. */
export function expectIdentical(actual: Buffer, expected: Buffer): void {
  if (actual.length !== expected.length) {
    throw new Error(`Length mismatch: received ${actual.length}, expected ${expected.length} bytes`);
  }

  const index = actual.findIndex((byte, position) => byte !== expected[position]);

  if (index >= 0) {
    throw new Error(`Data mismatch at byte ${index}: received 0x${actual[index].toString(16)}, ` +
      `expected 0x${expected[index].toString(16)}`);
  }
}
