import {
    buildMidiNodes, referenceClockLabel, clockStateLabel, formatBpm, isActive, loadWidgets, observationsByPort, saveWidgets, slugify,
    suggestedClockPort, validPortName, withApplicationPort, withRoute, withoutApplicationPort, WIDGET_STORAGE_KEY,
    formatRouteStatus, formatEndpointSummary, routeChannelClass, formatMidiWords
} from './midi-model';

const midi = {
    available: true,
    configuration: {
        virtualPorts: [{ id: 'bitwig', name: 'Bitwig', input: true, output: false }],
        routes: [{ id: 'r1', source: 'midi:virtual:bitwig', destination: 'midi:port:2:usb' }]
    },
    ports: [
        { id: 'midi:virtual:bitwig', name: 'Bitwig', kind: 'virtual', source: true, destination: false, online: true, open: true },
        { id: 'midi:port:1:usb', name: 'UM-ONE In', kind: 'port', endpointId: 'usb', endpointName: 'UM-ONE', group: 1,
            source: true, destination: false, online: true, open: true,
            clock: { ticks: 10, running: true, bpm: 98.5, echoSuspected: false } },
        { id: 'midi:port:2:usb', name: 'UM-ONE Out', kind: 'port', endpointId: 'usb', endpointName: 'UM-ONE', group: 2,
            source: false, destination: true, online: true, open: true },
        { id: 'midi:port:1:gone', name: '', kind: 'port', endpointId: 'gone', group: 1, source: false, destination: false, online: false }
    ],
    routes: [{ id: 'r1', active: true, forwarded: 3, filtered: 0 }]
};

test('formats route health and processing details for the graph', () => {
    expect(formatRouteStatus({ enabled: true, channels: [1, 2], messages: ['notes'], reclock: true },
        { active: true, forwarded: 1245, filtered: 12 })).toBe('Active · 1,245 messages · 12 filtered · channels 1–2 · reclocking');
    expect(formatRouteStatus({ enabled: false }, { active: false, forwarded: 0, filtered: 0 })).toBe('Disabled · 0 messages');
    expect(formatRouteStatus({ channels: [1, 2] }, { active: true, forwarded: 12, dominantChannel: 2 }))
        .toContain('dominant channel 2');
    expect(routeChannelClass({ channels: [4] }, { forwarded: 0 })).toBe('midi-channel-4');
    expect(routeChannelClass({ channels: [1, 2] }, { dominantChannel: 2 })).toBe('midi-channel-2');
    expect(routeChannelClass({ channels: [1, 2] }, { dominantChannel: 2, dominantChannelTie: true })).toBe('');
});

test('formats Windows MIDI Services endpoint capabilities without guessing missing values', () => {
    expect(formatEndpointSummary({ framework: 'windows-midi-services', transport: 'KS',
        manufacturer: 'ALLEN&HEATH LTD.', nativeDataFormat: 'MIDI 1.0 Byte Format',
        supportsMultiClient: true, activeClients: 5,
        clients: [{ processName: 'mixxx.exe', sessionName: 'Client WinMM' }] })).toBe(
        'Windows MIDI Services / Windows.Devices.Midi2 · KS · MIDI 1.0 Byte Format · multi-client · 5 clients · clients: mixxx.exe · ALLEN&HEATH LTD.');
    expect(formatEndpointSummary({ framework: 'windows-midi-services', transport: null,
        supportsMultiClient: null, activeClients: null })).toBe('Windows MIDI Services / Windows.Devices.Midi2');
});

test('formats monitored MIDI UMP words as stable uppercase hexadecimal', () => {
    expect(formatMidiWords([0x20903C64, 0x10F8])).toBe('20903C64 000010F8');
});

test('MIDI loopbacks show only enabled directions and devices group their MIDI 1.0 ports', () => {
    const nodes = buildMidiNodes(midi);
    const app = nodes.find(node => node.id === 'midi-application:bitwig');
    expect(app.role).toBe('loopback');
    expect(app.ports.map(port => [port.name, port.side])).toEqual([['Bitwig In', 'source']]);
    const device = nodes.find(node => node.id === 'midi-device:usb');
    expect(device.label).toBe('UM-ONE');
    expect(device.ports.map(port => [port.name, port.side])).toEqual([['UM-ONE In', 'source'], ['UM-ONE Out', 'destination']]);
    const gone = nodes.find(node => node.id === 'midi-device:gone');
    expect(gone.offline).toBe(true);
    expect(gone.ports[0].side).toBe('absent');
});

test('configuration edits never duplicate routes and removing a port removes its routes', () => {
    const configuration = midi.configuration;
    expect(withRoute(configuration, 'midi:virtual:bitwig', 'midi:port:2:usb', 1)).toBe(configuration);
    const added = withRoute(configuration, 'midi:port:1:usb', 'midi:virtual:bitwig', 1000);
    expect(added.routes).toHaveLength(2);
    expect(added.routes[1]).toEqual({ id: 'midi-rs', source: 'midi:port:1:usb', destination: 'midi:virtual:bitwig', enabled: true });
    expect(withoutApplicationPort(added, 'bitwig')).toEqual({ virtualPorts: [], routes: [] });
    const renamed = withApplicationPort(configuration, { id: 'bitwig', name: 'Bitwig 5', input: true, output: true });
    expect(renamed.virtualPorts).toEqual([{ id: 'bitwig', name: 'Bitwig 5', input: true, output: true }]);
});

test('names and ids follow the engine limits', () => {
    expect(slugify('Ableton Live 12 Suite!', new Set())).toBe('ableton-live-12-suit');
    expect(slugify('Bitwig', new Set(['bitwig']))).toBe('bitwig-2');
    expect(slugify('Éditeur', new Set())).toBe('editeur');
    expect(validPortName('Bitwig')).toBe(true);
    expect(validPortName('Éditeur')).toBe(false);
    expect(validPortName(' Bitwig')).toBe(false);
    expect(validPortName('x'.repeat(25))).toBe(false);
});

test('removing an application preserves the tempo, other configuration fields and unrelated routes', () => {
    const unrelated = { id: 'keep', source: 'midi:port:1:usb', destination: 'midi:port:2:usb' };
    const configuration = { ...midi.configuration, version: 1, engineClock: { bpm: 137 },
        routes: [...midi.configuration.routes, unrelated] };
    expect(withoutApplicationPort(configuration, 'bitwig')).toEqual({
        ...configuration, virtualPorts: [], routes: [unrelated]
    });
    expect(configuration.virtualPorts).toHaveLength(1);
    expect(configuration.routes).toHaveLength(2);
});

test('clock display separates running, measuring, echo and stopped states', () => {
    expect(formatBpm({ running: true, bpm: 120 })).toBe('120.0');
    expect(formatBpm({ running: true, bpm: null })).toBe('…');
    expect(formatBpm({ running: false, bpm: 120 })).toBe('—');
    expect(clockStateLabel(null)).toBe('No clock received');
    expect(clockStateLabel({ ticks: 4, running: false })).toBe('Clock stopped');
    expect(clockStateLabel({ ticks: 4, running: true, echoSuspected: true })).toBe('Possible echo of the patchbay clock');
    expect(clockStateLabel({ ticks: 4, running: true, bpm: null })).toBe('Measuring tempo');
    expect(isActive(1000, 1200)).toBe(true);
    expect(isActive(1000, 1400)).toBe(false);
    expect(isActive(null, 1000)).toBe(false);
});

test('the suggested BPM port is a real clock source, not an echo', () => {
    expect(suggestedClockPort(midi, {})).toBe('midi:port:1:usb');
    const observations = observationsByPort({ ports: [
        { portId: 'midi:port:1:usb', clock: { running: true, echoSuspected: true, ticks: 5 } },
        { portId: 'midi:virtual:bitwig', clock: { running: true, echoSuspected: false, ticks: 5 } }] });
    expect(suggestedClockPort(midi, observations)).toBe('midi:virtual:bitwig');
});

test('widgets are kept in local storage and invalid data is ignored', () => {
    const values = {};
    const storage = { getItem: key => values[key] ?? null, setItem: (key, value) => { values[key] = value; } };
    saveWidgets(storage, [{ id: 'w1', portId: 'midi:port:1:usb' }]);
    expect(loadWidgets(storage)).toEqual([{ id: 'w1', portId: 'midi:port:1:usb' }]);
    values[WIDGET_STORAGE_KEY] = '{broken';
    expect(loadWidgets(storage)).toEqual([]);
});

test('the reference clock label names the sound card or software clock frequency and drift', () => {
    expect(referenceClockLabel({ source: 'none' })).toBe('Reference clock: not running (start audio)');
    expect(referenceClockLabel({ source: 'asio', nominalSampleRate: 48000, driftPpm: null })).toBe('Reference clock: sound card 48000 Hz, measuring…');
    expect(referenceClockLabel({ source: 'asio', measuredSampleRate: 47999.04, driftPpm: -20 }))
        .toBe('Reference clock: sound card 47999.04 Hz (−20.0 ppm vs Windows counter)');
    expect(referenceClockLabel({ source: 'software', measuredSampleRate: 48000, driftPpm: 0 }))
        .toBe('Reference clock: software clock 48000.00 Hz (+0.0 ppm vs Windows counter)');
    const nodes = buildMidiNodes({ ports: [{ id: 'midi:clock:engine', kind: 'engineClock', source: true }] });
    expect(nodes[0]).toEqual(expect.objectContaining({ id: 'midi-engine-clock', kind: 'engineClock' }));
    expect(nodes[0].ports[0].side).toBe('source');
});
