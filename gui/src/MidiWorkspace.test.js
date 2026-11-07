import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import MidiWorkspace from './MidiWorkspace';

// A light React Flow: renders node contents and exposes the callbacks so tests
// can right-click blocks and cables and draw connections.
jest.mock('@xyflow/react', () => ({
    ReactFlow: props => {
        globalThis.__midiFlow = props;
        return <div data-testid="flow" onContextMenu={props.onPaneContextMenu}>
            {props.nodes.map(node => <div key={node.id} data-flow-node={node.id}
                onContextMenu={event => { event.stopPropagation(); props.onNodeContextMenu(event, node); }}>
                {node.data.render()}</div>)}
            {props.edges.map(edge => <button type="button" key={edge.id} data-flow-edge={edge.id} className={edge.className}
                onContextMenu={event => { event.stopPropagation(); props.onEdgeContextMenu(event, edge); }}>{edge.ariaLabel}</button>)}
            {props.children}
        </div>;
    },
    Background: () => null,
    Handle: ({ id }) => <span data-handle={id} />,
    Position: { Left: 'left', Right: 'right' },
    useReactFlow: () => ({ fitView: () => {} }),
    useNodesState: nodes => {
        const [state, setState] = require('react').useState(nodes);
        return [state, setState, () => {}];
    }
}));

const NOW = 1_700_000_000_000;
const document_ = (configuration, extra = {}) => ({
    available: true, backend: 'windows-midi-services', error: null, configuration,
    ports: [
        ...configuration.virtualPorts.map(port => ({ id: `midi:virtual:${port.id}`, name: port.name, kind: 'virtual',
            source: port.input !== false, destination: port.output !== false, online: true, open: true,
            inputEndpointId: 'in', outputEndpointId: null })),
        { id: 'midi:port:1:usb', name: 'UM-ONE In', kind: 'port', endpointId: 'usb', endpointName: 'UM-ONE', group: 1,
            source: true, destination: false, online: true, open: true,
            activity: { messages: 0, lastUnixMs: null }, clock: { ticks: 0, running: false, bpm: null } },
        { id: 'midi:port:2:usb', name: 'UM-ONE Out', kind: 'port', endpointId: 'usb', endpointName: 'UM-ONE', group: 2,
            source: false, destination: true, online: true, open: true }
    ],
    routes: configuration.routes.map(route => ({ id: route.id, active: true, forwarded: 0, filtered: 0 })),
    ...extra
});

function memoryStorage() {
    const values = {};
    return { values, getItem: key => values[key] ?? null, setItem: (key, value) => { values[key] = value; } };
}

async function mount(api, storage = memoryStorage()) {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    await act(async () => root.render(<MidiWorkspace api={api} storage={storage} now={NOW} />));
    return { container, storage, unmount: async () => { await act(async () => root.unmount()); container.remove(); } };
}

const menuItem = label => [...document.querySelectorAll('.patchbay-context-menu [role="menuitem"]')]
    .find(button => button.textContent === label);
const choose = async (target, label) => {
    await act(async () => Simulate.contextMenu(target, { clientX: 50, clientY: 60 }));
    const item = menuItem(label);
    expect(item).toBeDefined();
    await act(async () => Simulate.click(item));
};
const dialogButton = label => [...document.querySelectorAll('[role="dialog"] button')].find(button => button.textContent === label);

function fakeApi(initial) {
    let current = initial;
    const listeners = [];
    const api = {
        setMidiClock: jest.fn(async settings => {
            current = { ...current, engineClock: { ...current.engineClock,
                ...(settings.bpm ? { bpm: settings.bpm } : {}),
                ...(settings.transport ? { running: settings.transport !== 'stop', transport: settings.transport } : {}) } };
            return current;
        }),
        getMidi: jest.fn(async () => current),
        replaceMidiConfiguration: jest.fn(async configuration => { current = document_(configuration); return current; }),
        subscribeEvents: jest.fn(callback => { listeners.push(callback); return () => {}; }),
        emit: payload => listeners.forEach(listener => listener({ event: 'supervision.observations', payload }))
    };
    return api;
}

afterEach(() => { document.body.innerHTML = ''; });

test('the MIDI tab adds a MIDI loopback from the graph context menu', async () => {
    const api = fakeApi(document_({ virtualPorts: [], routes: [] }));
    const view = await mount(api);
    try {
        expect(view.container.querySelector('[data-flow-node="midi-device:usb"]').textContent).toContain('UM-ONE In');
        await choose(view.container.querySelector('[data-testid="flow"]'), 'Add MIDI loopback…');
        const name = document.querySelector('[role="dialog"] input:not([type])');
        await act(async () => Simulate.change(name, { target: { value: 'Bitwig' } }));
        const out = [...document.querySelectorAll('[role="dialog"] input[type="checkbox"]')][1];
        await act(async () => Simulate.change(out, { target: { checked: false } }));
        await act(async () => Simulate.click(dialogButton('Save')));
        expect(api.replaceMidiConfiguration).toHaveBeenCalledWith({ virtualPorts: [
            { id: 'bitwig', name: 'Bitwig', input: true, output: false }], routes: [] });
        const app = view.container.querySelector('[data-flow-node="midi-application:bitwig"]');
        expect(app.textContent).toContain('Bitwig In');
        expect(app.textContent).not.toContain('Bitwig Out');
    } finally { await view.unmount(); }
});

test('drawing a cable creates a MIDI route and its context menu deletes it', async () => {
    const api = fakeApi(document_({ virtualPorts: [{ id: 'bitwig', name: 'Bitwig', input: true, output: true }], routes: [] }));
    const view = await mount(api);
    try {
        const flow = globalThis.__midiFlow;
        expect(flow.isValidConnection({ sourceHandle: 'out:midi:virtual:bitwig', targetHandle: 'in:midi:virtual:bitwig' })).toBe(false);
        await act(async () => flow.onConnect({ source: 'midi-application:bitwig', target: 'midi-device:usb',
            sourceHandle: 'out:midi:virtual:bitwig', targetHandle: 'in:midi:port:2:usb' }));
        const sent = api.replaceMidiConfiguration.mock.calls[0][0];
        expect(sent.routes).toEqual([expect.objectContaining({ source: 'midi:virtual:bitwig', destination: 'midi:port:2:usb', enabled: true })]);
        const edge = view.container.querySelector(`[data-flow-edge="route:${sent.routes[0].id}"]`);
        expect(edge).not.toBeNull();
        await choose(edge, 'Delete connection');
        expect(api.replaceMidiConfiguration.mock.calls[1][0].routes).toEqual([]);
    } finally { await view.unmount(); }
});

test('a BPM display is assigned to a port and shows its clock, activity apart', async () => {
    const api = fakeApi(document_({ virtualPorts: [], routes: [] }));
    const view = await mount(api);
    try {
        await choose(view.container.querySelector('[data-testid="flow"]'), 'Add BPM display…');
        const select = document.querySelector('[role="dialog"] select');
        await act(async () => Simulate.change(select, { target: { value: 'midi:port:1:usb' } }));
        await act(async () => Simulate.click(dialogButton('Save')));
        const widget = () => view.container.querySelector('.midi-bpm-widget');
        expect(widget().textContent).toContain('UM-ONE · UM-ONE In');
        expect(widget().textContent).toContain('No clock received');
        expect(JSON.parse(view.storage.values['timox-vssc.midi.bpmWidgets'])[0].portId).toBe('midi:port:1:usb');
        expect(view.container.querySelector('[data-flow-edge^="widget:"]')).not.toBeNull();

        // Clock ticks light the clock light and the BPM, not the activity light.
        await act(async () => api.emit({ midi: { ports: [{ portId: 'midi:port:1:usb', online: true, open: true,
            activity: { messages: 0, lastUnixMs: null },
            clock: { ticks: 96, running: true, bpm: 120, lastTickUnixMs: NOW - 10, echoSuspected: false, transport: 'start' } }] } }));
        expect(widget().querySelector('.midi-bpm-value').textContent).toBe('120.0');
        expect(widget().textContent).toContain('start');
        expect(widget().querySelector('.midi-led.clock').className).toContain('on');
        const device = view.container.querySelector('[data-flow-node="midi-device:usb"]');
        expect(device.querySelector('.midi-led').className).not.toContain('on');
        expect(device.textContent).toContain('♩ 120.0');

        // An echoed clock is shown as such.
        await act(async () => api.emit({ midi: { ports: [{ portId: 'midi:port:1:usb',
            activity: { messages: 3, lastUnixMs: NOW - 50 },
            clock: { ticks: 98, running: true, bpm: 120, lastTickUnixMs: NOW - 10, echoSuspected: true } }] } }));
        expect(widget().textContent).toContain('Possible echo');
        expect(device.querySelector('.midi-led').className).toContain('on');

        // Removing the display only affects the presentation.
        await choose(view.container.querySelector('[data-flow-node^="bpm:"]'), 'Remove BPM display');
        expect(widget()).toBeNull();
        expect(api.replaceMidiConfiguration).not.toHaveBeenCalled();
    } finally { await view.unmount(); }
});

test('connecting a port to a BPM display assigns it without creating a route', async () => {
    const storage = memoryStorage();
    storage.setItem('timox-vssc.midi.bpmWidgets', JSON.stringify([{ id: 'w1', portId: '' }]));
    const api = fakeApi(document_({ virtualPorts: [], routes: [] }));
    const view = await mount(api, storage);
    try {
        expect(view.container.querySelector('.midi-bpm-widget').textContent).toContain('Not assigned');
        await act(async () => globalThis.__midiFlow.onConnect({ source: 'midi-device:usb', target: 'bpm:w1',
            sourceHandle: 'out:midi:port:1:usb', targetHandle: 'in:widget' }));
        expect(view.container.querySelector('.midi-bpm-widget').textContent).toContain('UM-ONE In');
        expect(api.replaceMidiConfiguration).not.toHaveBeenCalled();
    } finally { await view.unmount(); }
});

test('an unavailable MIDI engine is reported without blocking the tab', async () => {
    const api = fakeApi(null);
    api.getMidi.mockRejectedValue(new Error('The MIDI patchbay is not running in this engine'));
    const view = await mount(api);
    try {
        expect(view.container.querySelector('[role="alert"]').textContent).toContain('not running');
    } finally { await view.unmount(); }
});

test('missing Windows MIDI Services explains the required framework and no fallback', async () => {
    const api = fakeApi(document_({ virtualPorts: [], routes: [] }, {
        available: false, backend: '', error: 'Windows MIDI Services backend unavailable', ports: []
    }));
    const view = await mount(api);
    try {
        expect(view.container.querySelector('.midi-status').textContent)
            .toContain('Windows MIDI Services (Windows.Devices.Midi2) is not available');
        expect(view.container.querySelector('.patchbay-flow-empty').textContent)
            .toContain('no legacy MIDI fallback');
    } finally { await view.unmount(); }
});

test('the engine clock follows the reference clock and is driven from its block menu', async () => {
    const configuration = { virtualPorts: [], routes: [
        { id: 'keys-to-synth', source: 'midi:port:1:usb', destination: 'midi:port:2:usb', enabled: true }] };
    const base = document_(configuration);
    const api = fakeApi({ ...base,
        ports: [{ id: 'midi:clock:engine', name: 'Engine clock', kind: 'engineClock', source: true, destination: false,
            online: true, open: true, clock: { ticks: 0, running: false, bpm: null } }, ...base.ports],
        referenceClock: { source: 'asio', nominalSampleRate: 48000, measuredSampleRate: 48000.48, driftPpm: 10, measuredOverSeconds: 30 },
        engineClock: { bpm: 120, running: false, ticks: 0, transport: null } });
    const view = await mount(api);
    try {
        expect(view.container.querySelector('.midi-reference-clock').textContent)
            .toBe('Reference clock: sound card 48000.48 Hz (+10.0 ppm vs Windows counter)');
        const block = view.container.querySelector('[data-flow-node="midi-engine-clock"]');
        expect(block.textContent).toContain('Counted in sound card samples');
        expect(block.textContent).toContain('120.0');
        await choose(block, 'Start clock');
        expect(api.setMidiClock).toHaveBeenLastCalledWith({ transport: 'start' });
        expect(view.container.querySelector('[data-flow-node="midi-engine-clock"]').textContent).toContain('Running');
        await choose(view.container.querySelector('[data-flow-node="midi-engine-clock"]'), 'Set tempo…');
        await act(async () => Simulate.change(document.querySelector('[role="dialog"] input'), { target: { value: '98.5' } }));
        await act(async () => Simulate.click(dialogButton('Save')));
        expect(api.setMidiClock).toHaveBeenLastCalledWith({ bpm: 98.5 });
        expect(view.container.querySelector('[data-flow-node="midi-engine-clock"]').textContent).toContain('98.5');

        // An incoming clock can be reclocked on the card from the connection properties.
        await choose(view.container.querySelector('[data-flow-edge="route:keys-to-synth"]'), 'Connection properties…');
        const reclock = [...document.querySelectorAll('[role="dialog"] label.checkbox')]
            .find(label => label.textContent.startsWith('Reclock')).querySelector('input');
        await act(async () => Simulate.change(reclock, { target: { checked: true } }));
        await act(async () => Simulate.click(dialogButton('Save')));
        expect(api.replaceMidiConfiguration.mock.calls[0][0].routes[0]).toEqual(
            { id: 'keys-to-synth', source: 'midi:port:1:usb', destination: 'midi:port:2:usb', enabled: true, reclock: true });
    } finally { await view.unmount(); }
});

test('without a running reference clock the engine clock cannot be started and shows why', async () => {
    const base = document_({ virtualPorts: [], routes: [] });
    const api = fakeApi({ ...base,
        ports: [{ id: 'midi:clock:engine', name: 'Engine clock', kind: 'engineClock', source: true, destination: false,
            online: true, open: true, clock: { ticks: 0, running: false, bpm: null } }, ...base.ports],
        referenceClock: { source: 'none', nominalSampleRate: null, measuredSampleRate: null, driftPpm: null, measuredOverSeconds: 0 },
        engineClock: { bpm: 120, running: true, waitingForReferenceClock: true, ticks: 40, transport: 'start' } });
    const view = await mount(api);
    try {
        expect(view.container.querySelector('.midi-reference-clock').textContent).toBe('Reference clock: not running (start audio)');
        const block = view.container.querySelector('[data-flow-node="midi-engine-clock"]');
        expect(block.textContent).toContain('Waiting for the reference clock');
        await act(async () => Simulate.contextMenu(block, { clientX: 10, clientY: 10 }));
        expect(menuItem('Start clock').disabled).toBe(true);
    } finally { await view.unmount(); }
});
