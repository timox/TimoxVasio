const { spawnSync } = require('child_process');
const path = require('path');

const variant = process.argv[2];
if (variant !== 'tvsd') {
    console.error('Usage: node scripts/build-ui.js tvsd');
    process.exit(2);
}

const embedded = spawnSync(process.execPath, [path.join(__dirname, 'embed-openapi.js')], { stdio: 'inherit' });
if (embedded.status !== 0) process.exit(embedded.status || 1);

const build = spawnSync(process.execPath, [require.resolve('react-scripts/scripts/build')], {
    stdio: 'inherit',
    env: { ...process.env, REACT_APP_PRODUCT_NAME: 'TimoxVSoundDealer' }
});
process.exit(build.status === null ? 1 : build.status);
