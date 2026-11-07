// Run only after copying a freshly built engine and its receipt into artifacts/backend.
const fs = require('node:fs');
const path = require('node:path');
const { checkEngine } = require('./release-metadata.cjs');
const root = path.resolve(__dirname, '..');
const file = path.join(root, 'artifacts.lock.json');
const lock = JSON.parse(fs.readFileSync(file));
const receipt = JSON.parse(fs.readFileSync(path.join(root, 'artifacts/backend/engine-build.json')));
const engine = lock.artifacts.find(entry => entry.repository === 'TimoxVAudioEngine');
engine.revision = receipt.revision;
engine.sha256 = receipt.binarySha256;
engine.sizeBytes = fs.statSync(path.join(root, engine.path)).size;
delete engine.sourceLastWriteTimeUtc;
checkEngine(root, lock); // Reject wrong revision/version, dirty sources, MIDI OFF or changed binary before saving.
fs.writeFileSync(file, JSON.stringify(lock, null, 2) + '\n');
console.log('Verified engine receipt and binary pinned. Run release-preflight for all remaining inputs.');
