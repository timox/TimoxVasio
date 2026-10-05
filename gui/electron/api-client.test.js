const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const WebSocket = require('ws');
const { ApiClient } = require('./api-client');

async function createApiFixture() {
    const state = { apiVersion: '1.0', engine: { state: 'stopped' }, routes: [] };
    const drivers = { physicalDrivers: [], virtualDrivers: [] };
    const applicationProfiles = { profiles: [{ processName: 'mixxx.exe', inputChannels: 255, outputChannels: 255 }] };
    const diagnostics = { level: 'info', entries: [] };
    let diagnosticLevel = 'info';
    let submittedProfiles = null;
    const server = http.createServer((request, response) => {
        response.setHeader('content-type', 'application/json');
        if (request.url === '/api/v1/state') response.end(JSON.stringify(state));
        else if (request.url === '/api/v1/drivers') response.end(JSON.stringify(drivers));
        else if (request.url === '/api/v1/application-profiles' && request.method === 'GET')
            response.end(JSON.stringify(applicationProfiles));
        else if (request.url === '/api/v1/application-profiles' && request.method === 'PUT') {
            const chunks = [];
            request.on('data', chunk => chunks.push(chunk));
            request.on('end', () => {
                submittedProfiles = JSON.parse(Buffer.concat(chunks).toString());
                response.end(JSON.stringify({
                    profiles: submittedProfiles.profiles,
                    restartRequiredClients: [{ pid: 42, processName: 'AudioApp.exe' }]
                }));
            });
        }
        else if (request.url.startsWith('/api/v1/diagnostics') && request.method === 'GET')
            response.end(JSON.stringify({ ...diagnostics, level: diagnosticLevel }));
        else if (request.url === '/api/v1/diagnostics' && request.method === 'PUT') {
            const chunks = [];
            request.on('data', chunk => chunks.push(chunk));
            request.on('end', () => {
                diagnosticLevel = JSON.parse(Buffer.concat(chunks).toString()).level;
                response.end(JSON.stringify({ level: diagnosticLevel }));
            });
        }
        else { response.statusCode = 404; response.end('{}'); }
    });
    const sockets = new WebSocket.Server({ noServer: true });
    const connections = new Set();
    sockets.on('connection', socket => {
        connections.add(socket);
        socket.on('close', () => connections.delete(socket));
        socket.on('message', raw => {
            const command = JSON.parse(raw.toString());
            if (command.id === 'close-pending') return;
            if (command.id === 'fail') {
                socket.send(JSON.stringify({ id: command.id, success: false,
                    error: { code: 'INVALID_CONFIGURATION', message: 'configuration rejected' } }));
                return;
            }
            socket.send(JSON.stringify({ id: 'unrelated-response', success: true,
                result: { accepted: true } }));
            socket.send(JSON.stringify({ event: 'engine.status', payload: { state: 'reconfiguring' } }));
            socket.send(JSON.stringify({ id: command.id, success: true,
                result: { accepted: true } }));
        });
    });
    server.on('upgrade', (request, socket, head) => sockets.handleUpgrade(request, socket, head, ws => sockets.emit('connection', ws, request)));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const port = server.address().port;
    return {
        state, drivers, applicationProfiles,
        getSubmittedProfiles: () => submittedProfiles,
        client: new ApiClient({ baseUrl: `http://127.0.0.1:${port}`, webSocketUrl: `ws://127.0.0.1:${port}/api/v1/ws`, WebSocket }),
        close: async () => {
            for (const socket of connections) socket.terminate();
            sockets.close();
            await new Promise(resolve => server.close(resolve));
        }
    };
}

test('HTTP reads return the API state and driver inventory unchanged', async () => {
    const fixture = await createApiFixture();
    try {
        assert.deepEqual(await fixture.client.getState(), fixture.state);
        assert.deepEqual(await fixture.client.getDrivers(), fixture.drivers);
    } finally { await fixture.close(); }
});

test('application profiles use the documented GET and complete-replacement PUT routes', async () => {
    const fixture = await createApiFixture();
    const profiles = [{ processName: 'mixxx.exe', inputChannels: 255, outputChannels: 192 }];
    try {
        assert.deepEqual(await fixture.client.getApplicationProfiles(), fixture.applicationProfiles);
        const result = await fixture.client.replaceApplicationProfiles(profiles);
        assert.deepEqual(fixture.getSubmittedProfiles(), { profiles });
        assert.deepEqual(result, {
            profiles,
            restartRequiredClients: [{ pid: 42, processName: 'AudioApp.exe' }]
        });
    } finally { await fixture.close(); }
});

test('diagnostics use the documented HTTP read and level replacement routes', async () => {
    const fixture = await createApiFixture();
    try {
        assert.deepEqual(await fixture.client.getDiagnostics(40), { level: 'info', entries: [] });
        assert.deepEqual(await fixture.client.setDiagnosticLevel('debug'), { level: 'debug' });
        assert.deepEqual(await fixture.client.getDiagnostics(40), { level: 'debug', entries: [] });
    } finally { await fixture.close(); }
});

test('configuration.apply resolves only its matching response and forwards each event once', async () => {
    const fixture = await createApiFixture();
    const seen = [];
    try {
        await fixture.client.connect();
        fixture.client.on('event', event => seen.push(event));
        const result = await fixture.client.applyConfiguration({ routes: [] }, 'request-1');
        assert.deepEqual(result, { accepted: true });
        assert.deepEqual(seen, [{ event: 'engine.status', payload: { state: 'reconfiguring' } }]);
    } finally { fixture.client.close(); await fixture.close(); }
});

test('engine.stop uses the documented WebSocket command and resolves its acknowledgement', async () => {
    const fixture = await createApiFixture();
    try {
        await fixture.client.connect();
        assert.deepEqual(await fixture.client.stopEngine('stop-1'), { accepted: true });
    } finally { fixture.client.close(); await fixture.close(); }
});

test('engine.start is forwarded through the documented WebSocket API command', async () => {
    const fixture = await createApiFixture();
    const received = [];
    try {
        const original = fixture.client.sendCommand.bind(fixture.client);
        fixture.client.sendCommand = request => { received.push(request); return original(request); };
        await fixture.client.connect();
        assert.deepEqual(await fixture.client.sendCommand({ id: 'start-1', command: 'engine.start' }), { accepted: true });
        assert.deepEqual(received, [{ id: 'start-1', command: 'engine.start' }]);
    } finally { fixture.client.close(); await fixture.close(); }
});

test('the API console HTTP bridge only forwards documented local routes', async () => {
    const fixture = await createApiFixture();
    try {
        assert.deepEqual(await fixture.client.httpRequest('GET', '/api/v1/state'), { status: 200, body: fixture.state });
        await assert.rejects(fixture.client.httpRequest('GET', '/../secret'), /documentées/);
        await assert.rejects(fixture.client.httpRequest('POST', '/api/v1/state'), /documentées/);
    } finally { await fixture.close(); }
});

test('structured API errors reject with their stable error code', async () => {
    const fixture = await createApiFixture();
    try {
        await fixture.client.connect();
        await assert.rejects(fixture.client.applyConfiguration({}, 'fail'), error =>
            error.code === 'INVALID_CONFIGURATION' && error.message === 'configuration rejected');
    } finally { fixture.client.close(); await fixture.close(); }
});

test('a closed WebSocket rejects pending commands', async () => {
    const fixture = await createApiFixture();
    try {
        await fixture.client.connect();
        const pending = fixture.client.applyConfiguration({}, 'close-pending');
        fixture.client.close();
        await assert.rejects(pending, /closed|disconnect/i);
    } finally { await fixture.close(); }
});
