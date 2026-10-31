// A slow, incompatible or occupied API is not evidence that no engine exists.
// Only an explicitly refused loopback connection permits launching a process.
const { fetchJson } = require('./http-json');
async function discoverEngine(fetchImpl = fetch, { requiredApiVersion = null, requiredCapabilities = [], requiredSupervisionVersion = null } = {}) {
    const port = 52525;
    let response, state;
    try {
        ({ response, body: state } = await fetchJson(`http://127.0.0.1:${port}/api/v1/state`, {}, fetchImpl, 3000));
    } catch (error) {
        if (error.cause?.code === 'ECONNREFUSED') return null;
        throw new Error('Cannot reach the existing API. Refusing to start a second engine.', { cause: error });
    }
    try {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        if (!['1.0', '1.1'].includes(state?.apiVersion) ||
            (requiredApiVersion && state.apiVersion !== requiredApiVersion) ||
            !Array.isArray(state.virtualDrivers) ||
            !state.virtualDrivers.some(driver => driver?.id === 'TimoxVasio'))
            throw new Error('Unrecognized engine API');
        if (requiredCapabilities.some(capability => state.capabilities?.[capability] !== true) ||
            (requiredSupervisionVersion !== null && state.supervision?.schemaVersion !== requiredSupervisionVersion))
            throw new Error('The running engine lacks the capabilities or supervision schema required by this GUI');
    } catch (error) {
        const requirement = requiredApiVersion === '1.1'
            ? 'TimoxVSSC requires engine API 1.1'
            : 'The API port is occupied or incompatible';
        const failure = new Error(`${requirement}: ${error.message}. Refusing to start a second engine.`, { cause: error });
        failure.code = 'API_CONTRACT_MISMATCH';
        throw failure;
    }
    return port;
}
module.exports = { discoverEngine };
