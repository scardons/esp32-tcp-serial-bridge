import { BridgeClient } from './bridge-client.ts';
import { BLOCKED_PREFIX } from './config.ts';

// Every test file first proves that a client can get the single bridge slot.
// If it cannot, nothing was tested, so its tests are reported as blocked.
beforeAll(async () => {
  try {
    const client = await BridgeClient.connectActive(3000);

    await client.close();
  } catch (error) {
    throw new Error(`${BLOCKED_PREFIX} ${(error as Error).message}`);
  }
});
