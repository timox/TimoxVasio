require('./embed-openapi');
const { spawnSync } = require('node:child_process');
const result = spawnSync(process.execPath, [require.resolve('react-scripts/scripts/build')], { stdio: 'inherit' });
process.exit(result.status === null ? 1 : result.status);
