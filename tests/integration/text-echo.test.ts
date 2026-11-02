import { BridgeClient } from '../support/bridge-client.ts';

describe('H2: Bidirectional text echo', () => {
  let client: BridgeClient | undefined;

  beforeEach(async () => {
    client = await BridgeClient.connectActive();
  });

  afterEach(async () => {
    await client?.close();
  });

  test('H2: echoes a text line through UART1 and the loopback jumper', async () => {
    await client!.write('hola\n');

    const echo = await client!.readExactly(5, 3000);

    expect(echo.toString()).toBe('hola\n');
  });

  test('H2: echoes several messages in the order they were sent', async () => {
    const messages = ['first\r\n', 'second\r\n', 'third\r\n'];
    const expected = messages.join('');

    for (const message of messages) {
      await client!.write(message);
    }

    const echo = await client!.readExactly(expected.length, 3000);

    expect(echo.toString()).toBe(expected);
  });
});
