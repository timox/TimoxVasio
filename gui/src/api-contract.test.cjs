const test = require('node:test');
const assert = require('node:assert/strict');
const { configurationFromState, applyCommand, endpointLabel } = require('./api-contract.js');

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

test('configuration is derived solely from the documented API state', () => {
    assert.deepEqual(configurationFromState(state), {
        physicalDriverId: null,
        sampleRate: null,
        bufferFrames: null,
        routes: state.routes
    });
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
