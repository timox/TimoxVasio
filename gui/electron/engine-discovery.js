// A slow, incompatible or occupied API is not evidence that no engine exists.
// Only an explicitly refused loopback connection permits launching a process.
async function discoverEngine(fetchImpl = fetch) {
    const port = 52525;
    let response;
    try {
        response = await fetchImpl(`http://127.0.0.1:${port}/api/v1/state`, {
            signal: AbortSignal.timeout(3000)
        });
    } catch (error) {
        if (error.cause?.code === 'ECONNREFUSED') return null;
        throw new Error('Cannot reach the existing API. Refusing to start a second engine.', { cause: error });
    }
    let state;
    try {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        state = await response.json();
        if (!['1.0', '1.1'].includes(state?.apiVersion) ||
            !Array.isArray(state.virtualDrivers) ||
            !state.virtualDrivers.some(driver => driver?.id === 'TimoxVasio'))
            throw new Error('Unrecognized engine API');
    } catch (error) {
        throw new Error('The API port is occupied or incompatible. Refusing to start a second engine.', { cause: error });
    }
    return port;
}
module.exports = { discoverEngine };
