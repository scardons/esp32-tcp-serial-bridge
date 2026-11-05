function positiveInteger(name: string, fallback: number): number {
  const raw = process.env[name];

  if (raw === undefined || raw === '') {
    return fallback;
  }

  const value = Number(raw);

  if (!Number.isInteger(value) || value <= 0) {
    throw new Error(`${name} must be a positive integer, got "${raw}"`);
  }

  return value;
}

export const bridge = {
  host: process.env.BRIDGE_HOST || '192.168.1.200',
  port: positiveInteger('BRIDGE_PORT', 4001),
  connectTimeoutMs: positiveInteger('BRIDGE_CONNECT_TIMEOUT_MS', 3000),
};

/** UART1 rate in bytes per second, 10 bits per byte (8N1). Set BRIDGE_BAUD when the firmware uses another rate. */
export const BAUD_BYTES_PER_SECOND = positiveInteger('BRIDGE_BAUD', 115200) / 10;

/** Also read by the report: these failures mean that nothing could be tested. */
export const BLOCKED_PREFIX = 'BLOCKED:';

export const target = `${bridge.host}:${bridge.port}`;
