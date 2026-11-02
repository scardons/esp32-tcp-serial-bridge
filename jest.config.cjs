/** The bridge accepts one client at a time, so tests must never run in parallel. */
module.exports = {
  testEnvironment: 'node',
  testMatch: ['<rootDir>/tests/integration/**/*.test.ts'],
  transform: {
    '^.+\\.ts$': ['ts-jest', { tsconfig: 'tsconfig.json' }],
  },
  maxWorkers: 1,
  setupFilesAfterEnv: ['<rootDir>/tests/support/setup.ts'],
  testTimeout: 20000,
  verbose: true,
  reporters: ['default', '<rootDir>/tests/support/markdown-reporter.cjs'],
};
