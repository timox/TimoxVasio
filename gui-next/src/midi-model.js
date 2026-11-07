// Pure helpers for the MIDI tab: graph model, edits of the engine MIDI
// configuration, BPM widget storage. The engine owns ports, routes and
// observations; this file only presents them.

export const VIRTUAL_PREFIX = 'midi:virtual:';
export const PORT_PREFIX = 'midi:port:';
export const ENGINE_CLOCK_PORT = 'midi:clock:engine';
export const ENGINE_CLOCK_NODE = 'midi-engine-clock';
export const MESSAGE_CATEGORIES = [
    ['notes', 'Notes'], ['controlChange', 'Control change'], ['programChange', 'Program change'],
    ['pitchBend', 'Pitch bend'], ['channelPressure', 'Channel pressure'], ['polyPressure', 'Poly pressure'],
    ['systemRealtime', 'Clock and transport'], ['systemCommon', 'System common'], ['sysex', 'SysEx']
];
export const ACTIVITY_WINDOW_MS = 300;
export const WIDGET_STORAGE_KEY = 'timox-vssc.midi.bpmWidgets';
export const LAYOUT_STORAGE_KEY = 'timox-vssc.midi.layout';

export const applicationNodeId = id => `midi-application:${id}`;
export const deviceNodeId = endpointId => `midi-device:${endpointId}`;
export const widgetNodeId = id => `bpm:${id}`;

// Live counters come from the supervision.observations event when present,
// otherwise from the last GET /api/v1/midi document.
export function portObservation(port, observations) {
    const live = observations?.[port.id];
    return {
        activity: live?.activity ?? port.activity ?? null,
        clock: live?.clock ?? port.clock ?? null,
        online: live?.online ?? port.online,
        open: live?.open ?? port.open,
        error: live?.error ?? port.error ?? null
    };
}

export function observationsByPort(midiObservations) {
    const result = {};
    for (const port of midiObservations?.ports || []) result[port.portId] = port;
    return result;
}

export const isActive = (lastUnixMs, now) =>
    typeof lastUnixMs === 'number' && now - lastUnixMs >= 0 && now - lastUnixMs <= ACTIVITY_WINDOW_MS;

export function formatBpm(clock) {
    if (!clock?.running) return '—';
    return typeof clock.bpm === 'number' ? clock.bpm.toFixed(1) : '…';
}

export function clockStateLabel(clock) {
    if (!clock || !clock.ticks) return 'No clock received';
    if (!clock.running) return 'Clock stopped';
    if (clock.echoSuspected) return 'Possible echo of the patchbay clock';
    return typeof clock.bpm === 'number' ? 'Clock running' : 'Measuring tempo';
}

// One node per Timox MIDI loopback and per MIDI 1.0 device endpoint.
export function buildMidiNodes(midi) {
    const nodes = [];
    const devices = new Map();
    for (const port of midi?.ports || []) {
        if (port.kind === 'engineClock') {
            nodes.push({ id: ENGINE_CLOCK_NODE, kind: 'engineClock', label: 'Engine clock', resourceId: 'engine',
                ports: [{ id: port.id, name: 'Clock out', side: 'source', portRef: port }] });
        } else if (port.kind === 'virtual') {
            const id = port.id.slice(VIRTUAL_PREFIX.length);
            nodes.push({ id: applicationNodeId(id), kind: 'application', role: 'loopback', label: port.name || id, resourceId: id,
                ports: [
                    ...(port.source ? [{ id: port.id, name: `${port.name} In`, side: 'source', portRef: port }] : []),
                    ...(port.destination ? [{ id: port.id, name: `${port.name} Out`, side: 'destination', portRef: port }] : [])
                ] });
        } else if (port.kind === 'port') {
            const key = port.endpointId || port.id;
            if (!devices.has(key)) {
                const node = { id: deviceNodeId(key), kind: 'device', label: port.endpointName || port.name || key,
                    resourceId: key, ports: [] };
                devices.set(key, node);
                nodes.push(node);
            }
            const node = devices.get(key);
            if (!port.online) node.offline = true;
            const name = port.name || `Port ${port.group}`;
            if (port.source) node.ports.push({ id: port.id, name, side: 'source', portRef: port });
            if (port.destination) node.ports.push({ id: port.id, name, side: 'destination', portRef: port });
            if (!port.source && !port.destination)
                node.ports.push({ id: port.id, name: `${name} (absent)`, side: 'absent', portRef: port });
        }
    }
    for (const node of devices.values())
        if (node.ports.some(port => port.portRef.online)) node.offline = false;
    return nodes;
}

export function routeStatusById(midi) {
    return Object.fromEntries((midi?.routes || []).map(route => [route.id, route]));
}

export function formatRouteStatus(route, status = {}) {
    const parts = [route?.enabled === false || status.active === false ? 'Disabled' : 'Active'];
    const forwarded = Number(status.forwarded) || 0;
    const filtered = Number(status.filtered) || 0;
    parts.push(`${forwarded.toLocaleString('en-US')} message${forwarded === 1 ? '' : 's'}`);
    if (filtered) parts.push(`${filtered.toLocaleString('en-US')} filtered`);
    if (route?.channels?.length) {
        const channels = [...route.channels].sort((a, b) => a - b);
        parts.push(`channels ${channels.length === 1 ? channels[0] : `${channels[0]}–${channels[channels.length - 1]}`}`);
    }
    if (Number.isInteger(status.dominantChannel) && !status.dominantChannelTie)
        parts.push(`dominant channel ${status.dominantChannel}`);
    if (route?.reclock) parts.push('reclocking');
    return parts.join(' · ');
}

export function routeChannelClass(route, status = {}) {
    const channel = Number.isInteger(status.dominantChannel) && !status.dominantChannelTie
        ? status.dominantChannel
        : route?.channels?.length === 1 ? route.channels[0] : null;
    return Number.isInteger(channel) && channel >= 1 && channel <= 16 ? `midi-channel-${channel}` : '';
}

export function formatEndpointSummary(port) {
    const parts = ['Windows MIDI Services / Windows.Devices.Midi2'];
    if (port?.transport) parts.push(port.transport);
    if (port?.nativeDataFormat) parts.push(port.nativeDataFormat);
    if (port?.supportsMultiClient === true) parts.push('multi-client');
    if (Number.isInteger(port?.activeClients)) parts.push(`${port.activeClients} client${port.activeClients === 1 ? '' : 's'}`);
    const clients = (port?.clients || []).map(client => client.processName || client.sessionName).filter(Boolean);
    if (clients.length) parts.push(`clients: ${clients.join(', ')}`);
    if (port?.manufacturer) parts.push(port.manufacturer);
    return parts.join(' · ');
}

export const formatMidiWords = words => (words || []).map(word => Number(word).toString(16).toUpperCase().padStart(8, '0')).join(' ');

export const sourcePorts = midi => (midi?.ports || []).filter(port => port.source);

// Port that should be offered first for a BPM display: a running clock source
// that is not an echo, then any port that ever received clock.
export function suggestedClockPort(midi, observations) {
    const ports = sourcePorts(midi);
    const clockOf = port => portObservation(port, observations).clock;
    return ports.find(port => clockOf(port)?.running && !clockOf(port)?.echoSuspected)?.id
        || ports.find(port => clockOf(port)?.ticks)?.id || ports[0]?.id || '';
}

export function slugify(name, taken) {
    const base = name.toLocaleLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '')
        .replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 20) || 'app';
    let id = base, index = 2;
    while (taken.has(id)) id = `${base.slice(0, 20)}-${index++}`;
    return id;
}

export const validPortName = name => /^[ -~]{1,24}$/.test(name) && name.trim() === name;

export function newRouteId(configuration, now = Date.now()) {
    const taken = new Set((configuration?.routes || []).map(route => route.id));
    let id = `midi-${now.toString(36)}`, index = 2;
    while (taken.has(id)) id = `midi-${now.toString(36)}-${index++}`;
    return id;
}

// Configuration edits. Each returns a new configuration document for
// PUT /api/v1/midi/configuration.
export const emptyConfiguration = () => ({ virtualPorts: [], routes: [] });

export function withRoute(configuration, source, destination, now) {
    const current = configuration || emptyConfiguration();
    if (current.routes.some(route => route.source === source && route.destination === destination)) return current;
    return { ...current, routes: [...current.routes, { id: newRouteId(current, now), source, destination, enabled: true }] };
}

export const withoutRoute = (configuration, routeId) =>
    ({ ...configuration, routes: configuration.routes.filter(route => route.id !== routeId) });

export const withUpdatedRoute = (configuration, route) =>
    ({ ...configuration, routes: configuration.routes.map(item => item.id === route.id ? route : item) });

export function withApplicationPort(configuration, port) {
    const current = configuration || emptyConfiguration();
    const exists = current.virtualPorts.some(item => item.id === port.id);
    return { ...current, virtualPorts: exists
        ? current.virtualPorts.map(item => item.id === port.id ? port : item)
        : [...current.virtualPorts, port] };
}

export function withoutApplicationPort(configuration, id) {
    const portId = VIRTUAL_PREFIX + id;
    return { ...configuration, virtualPorts: configuration.virtualPorts.filter(port => port.id !== id),
        routes: configuration.routes.filter(route => route.source !== portId && route.destination !== portId) };
}

// BPM widgets are presentation only: kept in browser storage, never sent to the engine.
export function loadWidgets(storage) {
    try {
        const value = JSON.parse(storage?.getItem(WIDGET_STORAGE_KEY) || '[]');
        return Array.isArray(value) ? value.filter(widget => widget && typeof widget.id === 'string') : [];
    } catch { return []; }
}

export function saveWidgets(storage, widgets) {
    try { storage?.setItem(WIDGET_STORAGE_KEY, JSON.stringify(widgets)); } catch { /* storage full or disabled */ }
}

export function loadLayout(storage) {
    try {
        const value = JSON.parse(storage?.getItem(LAYOUT_STORAGE_KEY) || '{}');
        return value && typeof value === 'object' && !Array.isArray(value) ? value : {};
    } catch { return {}; }
}

export function saveLayout(storage, layout) {
    try { storage?.setItem(LAYOUT_STORAGE_KEY, JSON.stringify(layout)); } catch { /* storage full or disabled */ }
}

// Default positions: devices that send on the left, applications in the
// middle, devices that only receive on the right, widgets above.
// Text for the reference clock: the clock that paces the audio graph (ASIO
// card or software clock). MIDI timing follows it, never Windows timers. It
// is not a MIDI clock master/slave role.
export function referenceClockLabel(referenceClock) {
    if (!referenceClock) return '';
    if (referenceClock.source === 'none' || !referenceClock.source) return 'Reference clock: not running (start audio)';
    const clock = referenceClock.source === 'software' ? 'software clock' : 'sound card';
    if (typeof referenceClock.driftPpm !== 'number') return `Reference clock: ${clock} ${referenceClock.nominalSampleRate} Hz, measuring…`;
    const sign = referenceClock.driftPpm >= 0 ? '+' : '−';
    return `Reference clock: ${clock} ${referenceClock.measuredSampleRate.toFixed(2)} Hz (${sign}${Math.abs(referenceClock.driftPpm).toFixed(1)} ppm vs Windows counter)`;
}

export function defaultPosition(node, index) {
    if (node.kind === 'widget') return { x: 40 + index * 200, y: -170 };
    if (node.kind === 'engineClock') return { x: 40, y: -170 };
    const column = node.kind === 'application' ? 1
        : node.ports.some(port => port.side === 'source') ? 0 : 2;
    return { x: 40 + column * 360, y: 20 + index * 150 };
}
