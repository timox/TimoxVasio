const test = require('node:test');
const assert = require('node:assert/strict');
const { configurationFromState, applyCommand, endpointLabel } = require('./api-contract.js');

test('software clock configuration survives state decoding without runtime capabilities', () => {
    const decoded = configurationFromState({ ...state,
        engine: { ...state.engine, physicalDriverId: null, sampleRate: 48000, bufferFrames: 256,
            clockSource: { kind: 'software' }, clockCapabilities: { inputChannels: 0 } } });
    assert.deepEqual(decoded.clockSource, { kind: 'software' });
    assert.equal('clockCapabilities' in decoded, false);
});

const state = {
    apiVersion: '1.0',
    engine: { state: 'stopped', physicalDriverId: null, sampleRate: null, bufferFrames: null, lastError: null },
    physicalDrivers: [{
        id: 'device-1', name: 'Interface', inputEndpoints: [], outputEndpoints: [],
        sampleRates: [44100, 48000], bufferSizes: null, capabilitiesKnown: false
    }],
    virtualDrivers: [{ id: 'TimoxVasio', channelsPerDirection: 256, sampleRate: null, bufferFrames: null,
        clients: [], inputEndpoints: [], outputEndpoints: [] }],
    routes: [{ id: 'r1', sourceEndpointId: 'out', destinationEndpointId: 'in', gainDb: -3, mute: false }]
};

test('API 1.1 copies desired bridges, never runtime state, and legacy omits bridges', () => {
    const bridge = { id: 'mic', kind: 'capture', defaultRole: 'communications', enabled: true, bufferDuration100ns: 0 };
    const decoded = configurationFromState({ ...state, apiVersion: '1.1', configuredWasapiBridges: [bridge], wasapiBridges: [{ id: 'mic', state: 'failed' }] });
    assert.deepEqual(decoded.wasapiBridges, [bridge]);
    assert.notEqual(decoded.wasapiBridges[0], bridge);
    assert.equal('wasapiBridges' in configurationFromState({ ...state, configuredWasapiBridges: [bridge] }), false);
    assert.equal('wasapiBridges' in configurationFromState({ ...state, apiVersion: '1.1' }), false);
    assert.deepEqual(configurationFromState({ ...state, apiVersion: '1.1', configuredWasapiBridges: [] }).wasapiBridges, []);
});

test('configuration is derived solely from the documented API state', () => {
    assert.deepEqual(configurationFromState(state), {
        physicalDriverId: null,
        sampleRate: null,
        bufferFrames: null,
        routes: state.routes
    });
});

test('a disconnected application keeps its configured route in the editor', () => {
    const pending = { id: 'pending', sourceEndpointId: 'virtual:TimoxVasio:app:renoise.exe:output:1',
        destinationEndpointId: 'physical:device-1:output:1', gainDb: 0, mute: false };
    const draft = configurationFromState({ ...state, routes: [], configuredRoutes: [pending] });
    assert.deepEqual(draft.routes, [pending]);
});

test('an active PID never replaces the stable saved application endpoint in the draft', () => {
    const saved = { id: 'r1', sourceEndpointId: 'virtual:TimoxVasio:app:traktor pro 4.exe:output:1',
        destinationEndpointId: 'physical:device-1:output:1', gainDb: 0, mute: false };
    const active = { ...saved, sourceEndpointId: 'virtual:TimoxVasio:916:output:1' };
    assert.deepEqual(configurationFromState({ ...state, configuredRoutes: [saved], routes: [active] }).routes,
        [saved]);
});

test('configuration.apply has the documented command envelope and no legacy route fields', () => {
    const command = applyCommand(configurationFromState(state), 'gui-1');
    assert.deepEqual(command, { id: 'gui-1', command: 'configuration.apply', payload: configurationFromState(state) });
    assert.equal('name' in command.payload.routes[0], false);
    assert.equal('sourceDriver' in command.payload.routes[0], false);
});

test('endpoint labels use names and stable API endpoint identifiers', () => {
    assert.equal(endpointLabel({ id: 'physical:device-1:output:2', name: 'Interface Out 2', direction: 'output', channel: 2 }),
        'Interface Out 2 (physical:device-1:output:2)');
});
