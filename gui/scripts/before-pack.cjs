const { verify } = require('./verify-artifacts.cjs');
module.exports = async context => {
    const root = context.packager.info.appDir;
    const lock = require('node:fs').readFileSync(require('node:path').join(root, 'artifacts.lock.json'), 'utf8');
    require('./release-metadata.cjs').checkSource(root, JSON.parse(lock));
    verify(root);
    require('./release-metadata.cjs').writeManifest(root, JSON.parse(lock));
};


