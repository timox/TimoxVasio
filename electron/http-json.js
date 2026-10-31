// Bound both the headers and response body; a connected but wedged API must not freeze the GUI.
async function fetchJson(url, options = {}, fetchImpl = fetch, timeoutMs = 10000) {
    const controller = new AbortController();
    let timer;
    try {
        return await Promise.race([
            (async () => {
                const response = await fetchImpl(url, { ...options, signal: controller.signal });
                return { response, body: await response.json() };
            })(),
            new Promise((_, reject) => { timer = setTimeout(() => {
                reject(new Error(`Engine API request timed out after ${timeoutMs} ms`));
                controller.abort();
            }, timeoutMs); })
        ]);
    } finally { clearTimeout(timer); }
}
module.exports = { fetchJson };
