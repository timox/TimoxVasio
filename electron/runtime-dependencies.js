const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const artifacts = ['artifacts/backend/TimoxVirtualAsioEngine.exe', 'artifacts/backend/PwarAsioClient.exe'];
const schemas = ['api/openapi-v1.json', 'api/schemas/api-v1.json'];
const hash = data => crypto.createHash('sha256').update(data).digest('hex');

function readBounded(file, maximumBytes) {
    const fd = fs.openSync(file, 'r');
    try {
        const stat = fs.fstatSync(fd);
        if (!stat.isFile() || stat.size < 1 || stat.size > maximumBytes) throw new Error('Invalid dependency file size');
        const data = Buffer.alloc(stat.size + 1);
        let total = 0, count;
        while (total < data.length && (count = fs.readSync(fd, data, total, data.length - total, null)) > 0) total += count;
        if (total !== stat.size) throw new Error('Dependency changed during validation');
        return data.subarray(0, total);
    } finally { fs.closeSync(fd); }
}

function validateEntries(entries, expected, revisions) {
    if (!Array.isArray(entries) || entries.length !== expected.length) throw new Error('Invalid dependency list');
    const paths = new Set();
    for (const entry of entries) {
        if (!entry || !expected.includes(entry.path) || paths.has(entry.path) ||
            typeof entry.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(entry.sha256) ||
            (revisions && (typeof entry.revision !== 'string' || !entry.revision.trim() || entry.revision.length > 256)))
            throw new Error('Invalid dependency pin');
        paths.add(entry.path);
    }
}

function validateRuntimeDependencies({ appRoot, resourcesPath, isPackaged } = {}) {
    try {
        if (typeof appRoot !== 'string' || !path.isAbsolute(appRoot) || typeof isPackaged !== 'boolean' ||
            (isPackaged && (typeof resourcesPath !== 'string' || !path.isAbsolute(resourcesPath))))
            throw new Error('Invalid runtime locations');
        const lock = JSON.parse(readBounded(path.join(appRoot, 'artifacts.lock.json'), 65536));
        if (lock?.formatVersion !== 1 || lock.requiredApiVersion !== '1.1') throw new Error('Incompatible dependency contract version');
        if (!Array.isArray(lock.requiredCapabilities) || lock.requiredCapabilities.length > 64 ||
            !lock.requiredCapabilities.includes('processCopyTemplates') ||
            new Set(lock.requiredCapabilities).size !== lock.requiredCapabilities.length ||
            lock.requiredCapabilities.some(value => typeof value !== 'string' || !/^[A-Za-z][A-Za-z0-9]{0,63}$/.test(value)))
            throw new Error('Invalid required engine capabilities');
        validateEntries(lock.artifacts, artifacts, true);
        validateEntries(lock.schemas, schemas, false);
        const located = new Map();
        for (const entry of [...lock.artifacts, ...lock.schemas]) {
            const binary = artifacts.includes(entry.path);
            // Lock paths are an exact allowlist; packaged dependencies never fall back to a checkout.
            const file = isPackaged ? path.join(resourcesPath, 'backend', binary ? path.basename(entry.path) : entry.path.slice(4))
                : path.join(appRoot, entry.path);
            const data = readBounded(file, binary ? 64 * 1024 * 1024 : 4 * 1024 * 1024);
            if (hash(data) !== entry.sha256) throw new Error(`Dependency hash mismatch: ${entry.path}`);
            located.set(entry.path, file);
            if (!binary) {
                const document = JSON.parse(data);
                const versions = document.definitions?.state?.properties?.apiVersion?.enum;
                const compatible = entry.path === schemas[0]
                    ? typeof document.info?.version === 'string' && /^1\.1\.\d+$/.test(document.info.version)
                    : Array.isArray(versions) && versions.includes(lock.requiredApiVersion);
                if (!compatible) throw new Error(`Incompatible API schema: ${entry.path}`);
            }
        }
        return { enginePath: located.get(artifacts[0]),
            engineSha256: lock.artifacts.find(entry => entry.path === artifacts[0]).sha256,
            requiredApiVersion: lock.requiredApiVersion, requiredCapabilities: [...lock.requiredCapabilities] };
    } catch (cause) {
        const error = new Error(`Runtime dependencies could not be validated: ${cause.message}`, { cause });
        error.code = 'RUNTIME_DEPENDENCY_INVALID';
        throw error;
    }
}

module.exports = { validateRuntimeDependencies };
