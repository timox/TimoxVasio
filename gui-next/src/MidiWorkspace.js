import React, { useEffect, useMemo, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { ReactFlow, Background, Handle, Position, useNodesState, useReactFlow } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import './MidiWorkspace.css';
import {
    ENGINE_CLOCK_PORT, MESSAGE_CATEGORIES, VIRTUAL_PREFIX, buildMidiNodes, referenceClockLabel, clockStateLabel, defaultPosition, emptyConfiguration,
    formatBpm, formatEndpointSummary, formatMidiWords, formatRouteStatus, isActive, loadLayout, loadWidgets, observationsByPort, portObservation, routeChannelClass, routeStatusById,
    saveLayout, saveWidgets, slugify, sourcePorts, suggestedClockPort, validPortName, widgetNodeId,
    withApplicationPort, withRoute, withUpdatedRoute, withoutApplicationPort, withoutRoute
} from './midi-model';

// Handles carry the side so a virtual port can be both a source and a destination.
const outHandle = portId => `out:${portId}`;
const inHandle = portId => `in:${portId}`;
const WIDGET_HANDLE = 'in:widget';

const MidiFlowNode = ({ data }) => data.render();
const nodeTypes = { midi: MidiFlowNode };

// Recenters when blocks appear or disappear (ports load after the first render).
function MidiFitView({ nodeCount }) {
    const { fitView } = useReactFlow();
    const previous = useRef(0);
    useEffect(() => {
        if (previous.current === nodeCount) return undefined;
        previous.current = nodeCount;
        if (!nodeCount) return undefined;
        const timer = window.setTimeout(() => fitView({ padding: 0.2, maxZoom: 1, duration: 0 }), 50);
        return () => window.clearTimeout(timer);
    }, [nodeCount, fitView]);
    return <div className="patchbay-flow-controls" role="group" aria-label="Graph zoom">
        <button type="button" onClick={() => fitView({ padding: 0.2, maxZoom: 1, duration: 0 })}>Recenter graph</button>
    </div>;
}

function MidiCanvas({ modelNodes, edges, isValidConnection, onConnect, onSavePosition,
    onPaneContextMenu, onNodeContextMenu, onEdgeContextMenu }) {
    const [nodes, setNodes, onNodesChange] = useNodesState(modelNodes);
    useEffect(() => {
        setNodes(current => {
            const existing = new Map(current.map(node => [node.id, node]));
            return modelNodes.map(node => {
                const old = existing.get(node.id);
                return old ? { ...old, ...node, position: old.position, measured: old.measured } : node;
            });
        });
    }, [modelNodes, setNodes]);
    return <ReactFlow nodes={nodes} edges={edges} nodeTypes={nodeTypes}
        fitView fitViewOptions={{ padding: 0.2, maxZoom: 1 }} minZoom={0.4} maxZoom={2}
        panOnDrag zoomOnScroll={false} zoomOnPinch zoomOnDoubleClick={false}
        deleteKeyCode={null} selectionKeyCode={null} isValidConnection={isValidConnection}
        onNodesChange={onNodesChange}
        onNodeDragStop={(event, node) => onSavePosition(node.id, node.position)}
        onConnect={onConnect}
        onPaneContextMenu={event => { event.preventDefault(); onPaneContextMenu(event); }}
        onNodeContextMenu={(event, node) => { event.preventDefault(); onNodeContextMenu(event, node.id); }}
        onEdgeContextMenu={(event, edge) => { event.preventDefault(); onEdgeContextMenu(event, edge.id); }}>
        <Background color="#dbe5ea" gap={20} />
        <MidiFitView nodeCount={modelNodes.length} />
    </ReactFlow>;
}

function Modal({ label, onClose, children }) {
    return createPortal(<div className="routing-dialog-backdrop" onMouseDown={event => {
        if (event.target === event.currentTarget) onClose();
    }} onKeyDown={event => { if (event.key === 'Escape') onClose(); }}>
        <div className="routing-dialog midi-dialog" role="dialog" aria-modal="true" aria-label={label}>
            <div className="routing-dialog-heading"><h2>{label}</h2>
                <button type="button" onClick={onClose}>Close</button></div>
            {children}
        </div>
    </div>, document.body);
}

function ApplicationPortDialog({ port, takenIds, onSave, onClose }) {
    const [name, setName] = useState(port?.name || '');
    const [input, setInput] = useState(port ? port.input !== false : true);
    const [output, setOutput] = useState(port ? port.output !== false : true);
    const nameValid = validPortName(name);
    const valid = nameValid && (input || output);
    return <Modal label={port ? `MIDI loopback ${port.name}` : 'New MIDI loopback'} onClose={onClose}>
        <p className="hint">Each direction is a separate Windows MIDI Services Basic MIDI 1.0 loopback
            that an application can select in its own MIDI settings.</p>
        <label className="field">Name shown to applications
            <input value={name} maxLength={24} onChange={event => setName(event.target.value)} autoFocus /></label>
        {!nameValid && name && <p className="midi-dialog-error">Use 1 to 24 plain characters (no accents), without leading or trailing spaces.</p>}
        <label className="checkbox"><input type="checkbox" checked={input} onChange={event => setInput(event.target.checked)} />
            “{name || 'Name'} In”: the application sends to the patchbay</label>
        <label className="checkbox"><input type="checkbox" checked={output} onChange={event => setOutput(event.target.checked)} />
            “{name || 'Name'} Out”: the patchbay sends to the application</label>
        {!input && !output && <p className="midi-dialog-error">Keep at least one direction.</p>}
        <div className="dialog-actions">
            <button type="button" onClick={onClose}>Cancel</button>
            <button type="button" className="primary" disabled={!valid} onClick={() =>
                onSave({ id: port?.id || slugify(name, takenIds), name, input, output })}>Save</button>
        </div>
    </Modal>;
}

function RouteDialog({ route, title, onSave, onDelete, onClose }) {
    const [enabled, setEnabled] = useState(route.enabled !== false);
    const [reclock, setReclock] = useState(route.reclock === true);
    const fromEngineClock = route.source === ENGINE_CLOCK_PORT;
    const [channels, setChannels] = useState(new Set(route.channels || Array.from({ length: 16 }, (_, i) => i + 1)));
    const [messages, setMessages] = useState(new Set(route.messages || MESSAGE_CATEGORIES.map(([id]) => id)));
    const [outputChannel, setOutputChannel] = useState(route.outputChannel ?? '');
    const [transpose, setTranspose] = useState(route.transpose ?? 0);
    const toggle = (set, setter, value) => setter(() => {
        const next = new Set(set);
        if (next.has(value)) next.delete(value); else next.add(value);
        return next;
    });
    const transposeValid = Number.isInteger(Number(transpose)) && Math.abs(Number(transpose)) <= 127;
    const valid = channels.size > 0 && messages.size > 0 && transposeValid;
    const save = () => {
        const next = { id: route.id, source: route.source, destination: route.destination, enabled };
        if (channels.size < 16) next.channels = [...channels].sort((a, b) => a - b);
        if (messages.size < MESSAGE_CATEGORIES.length) next.messages = MESSAGE_CATEGORIES.map(([id]) => id).filter(id => messages.has(id));
        if (outputChannel !== '') next.outputChannel = Number(outputChannel);
        if (Number(transpose) !== 0) next.transpose = Number(transpose);
        if (reclock && !fromEngineClock) next.reclock = true;
        onSave(next);
    };
    return <Modal label="MIDI connection properties" onClose={onClose}>
        <p className="hint">{title}</p>
        <label className="checkbox"><input type="checkbox" checked={enabled} onChange={event => setEnabled(event.target.checked)} />
            Connection enabled</label>
        {!fromEngineClock && <label className="checkbox"><input type="checkbox" checked={reclock}
            onChange={event => setReclock(event.target.checked)} />
            Reclock on the reference clock: re-emit clock ticks on a regular grid in reference clock samples (removes jitter, keeps the source tempo)</label>}
        <fieldset className="midi-channel-grid"><legend>Source channels</legend>
            {Array.from({ length: 16 }, (_, i) => i + 1).map(channel =>
                <label key={channel}><input type="checkbox" checked={channels.has(channel)}
                    onChange={() => toggle(channels, setChannels, channel)} />{channel}</label>)}
        </fieldset>
        <fieldset className="midi-message-grid"><legend>Messages</legend>
            {MESSAGE_CATEGORIES.map(([id, label]) =>
                <label key={id}><input type="checkbox" checked={messages.has(id)}
                    onChange={() => toggle(messages, setMessages, id)} />{label}</label>)}
        </fieldset>
        <label className="field">Send on channel
            <select value={outputChannel} onChange={event => setOutputChannel(event.target.value)}>
                <option value="">Unchanged</option>
                {Array.from({ length: 16 }, (_, i) => i + 1).map(channel => <option key={channel} value={channel}>{channel}</option>)}
            </select></label>
        <label className="field">Transpose (semitones)
            <input type="number" min="-127" max="127" value={transpose} onChange={event => setTranspose(event.target.value)} /></label>
        {!valid && <p className="midi-dialog-error">Keep at least one channel and one message type; transpose is −127 to 127.</p>}
        <div className="dialog-actions">
            <button type="button" className="danger" onClick={onDelete}>Delete connection</button>
            <button type="button" onClick={onClose}>Cancel</button>
            <button type="button" className="primary" disabled={!valid} onClick={save}>Save</button>
        </div>
    </Modal>;
}

function TempoDialog({ bpm, onSave, onClose }) {
    const [value, setValue] = useState(String(bpm ?? 120));
    const number = Number(value);
    const valid = Number.isFinite(number) && number >= 20 && number <= 300;
    return <Modal label="Engine clock tempo" onClose={onClose}>
        <p className="hint">Ticks are counted in sound card samples, so this clock never drifts from the audio.</p>
        <label className="field">Tempo (BPM)
            <input type="number" min="20" max="300" step="0.1" value={value} onChange={event => setValue(event.target.value)} autoFocus /></label>
        {!valid && <p className="midi-dialog-error">Use a tempo between 20 and 300 BPM.</p>}
        <div className="dialog-actions">
            <button type="button" onClick={onClose}>Cancel</button>
            <button type="button" className="primary" disabled={!valid} onClick={() => onSave(number)}>Save</button>
        </div>
    </Modal>;
}

function ClockSourceDialog({ ports, source, onSave, onClose }) {
    const [value, setValue] = useState(source || '');
    const choices = ports.filter(port => port.kind === 'port' && port.source && port.online);
    return <Modal label="MIDI clock source" onClose={onClose}>
        <p className="hint">Use standard MIDI Clock (F8) from a device or application. The engine measures its tempo and regenerates the output on the audio reference clock.</p>
        <label className="field">Source port
            <select value={value} onChange={event => setValue(event.target.value)} autoFocus>
                <option value="">Manual BPM</option>
                {choices.map(port => <option key={port.id} value={port.id}>{port.name} · {port.endpointName || port.endpointId}</option>)}
            </select>
        </label>
        {!choices.length && <p className="hint">No native MIDI source port is currently available.</p>}
        <div className="dialog-actions">
            <button type="button" onClick={onClose}>Cancel</button>
            <button type="button" className="primary" onClick={() => onSave(value)}>Save</button>
        </div>
    </Modal>;
}

function MidiLearnDialog({ api, engineClock, ports, onChanged, onClose }) {
    const [slot, setSlot] = useState('beat');
    const sourcePorts = ports.filter(port => (port.kind === 'port' || port.kind === 'virtual') && port.source && port.online);
    const [port, setPort] = useState(sourcePorts[0]?.id || '');
    const [state, setState] = useState(engineClock || {});
    const [busy, setBusy] = useState(false);
    const [error, setError] = useState('');
    const signal = state.learnedSignals?.[slot];
    useEffect(() => {
        if (!state.learningSlot || !api?.getMidi) return undefined;
        const timer = window.setInterval(async () => {
            try { const next = await api.getMidi(); setState(next.engineClock || {}); } catch (_) { /* dialog stays open */ }
        }, 250);
        return () => window.clearInterval(timer);
    }, [api, state.learningSlot]);
    const request = async action => {
        setBusy(true);
        setError('');
        try {
            const next = await api.learnMidiClock({ slot, action, ...(action === 'begin' ? { port } : {}) });
            setState(next.engineClock || {});
            onChanged(next);
        } catch (requestError) {
            setError(requestError?.message || 'MIDI Learn could not open this port.');
        } finally { setBusy(false); }
    };
    return <Modal label="MIDI Learn clock" onClose={onClose}>
        <p className="hint">Learn one signal at a time. Beat, Start and Stop are required; Continue is optional.</p>
        <label className="field">Signal to learn
            <select value={slot} onChange={event => setSlot(event.target.value)} disabled={busy || !!state.learningSlot}>
                <option value="beat">Beat</option><option value="start">Start</option>
                <option value="stop">Stop</option><option value="continue">Continue (optional)</option>
            </select>
        </label>
        <label className="field">Source port
            <select value={port} onChange={event => setPort(event.target.value)} disabled={busy || !!state.learningSlot}>
                <option value="">Select a MIDI source</option>
                {sourcePorts.map(item => <option key={item.id} value={item.id}>{item.name} · {item.endpointName || item.endpointId}</option>)}
            </select>
        </label>
        <p className="midi-learn-state">{state.learningSlot ? `Waiting for ${state.learningSlot}…`
            : signal ? `Captured on ${signal.port} · status 0x${Number(signal.status).toString(16).toUpperCase()}`
                : 'No signal learned yet.'}</p>
        {error && <p className="midi-dialog-error" role="alert">{error}</p>}
        <div className="dialog-actions">
            <button type="button" onClick={() => request('cancel')} disabled={busy || !state.learningSlot}>Cancel learn</button>
            <button type="button" onClick={() => request('begin')} disabled={busy || !!state.learningSlot || !port}>Learn signal</button>
            <button type="button" className="primary" onClick={() => request('commit')} disabled={busy || !signal || !state.learningSlot}>Keep signal</button>
        </div>
    </Modal>;
}

function WidgetDialog({ widget, choices, suggested, onSave, onClose }) {
    const [portId, setPortId] = useState(widget?.portId || suggested || '');
    return <Modal label={widget ? 'Assign BPM display' : 'New BPM display'} onClose={onClose}>
        <p className="hint">The display shows the tempo of the MIDI clock arriving on the chosen port.
            A clock has no sender address: the port it arrives on identifies its emitter.</p>
        <label className="field">Port to observe
            <select value={portId} onChange={event => setPortId(event.target.value)} autoFocus>
                <option value="">Not assigned</option>
                {choices.map(choice => <option key={choice.id} value={choice.id}>{choice.label}</option>)}
            </select></label>
        {!choices.length && <p className="hint">No port can send MIDI yet. Add a MIDI loopback with its In enabled, or connect a MIDI 1.0 device.</p>}
        <div className="dialog-actions">
            <button type="button" onClick={onClose}>Cancel</button>
            <button type="button" className="primary" onClick={() => onSave(portId)}>Save</button>
        </div>
    </Modal>;
}

function MidiMonitorDialog({ api, port, baselineSequence, onClose }) {
    const [current, setCurrent] = useState(port);
    const [error, setError] = useState('');
    useEffect(() => {
        let cancelled = false;
        const refresh = async () => {
            try {
                const snapshot = await api.getMidi();
                if (!cancelled) {
                    setCurrent(snapshot.ports?.find(item => item.id === port.id) || { ...port, recentMessages: [] });
                    setError('');
                }
            } catch (failure) { if (!cancelled) setError(failure.message || 'MIDI monitor unavailable'); }
        };
        refresh();
        const timer = window.setInterval(refresh, 250);
        return () => { cancelled = true; window.clearInterval(timer); };
    }, [api, port]);
    const messages = (current.recentMessages || []).filter(message => message.sequence > baselineSequence).slice().reverse();
    return <Modal label={`MIDI monitor · ${port.name}`} onClose={onClose}>
        <p className="hint">Messages received on this loopback after opening the monitor. The engine keeps a bounded buffer of the latest 64 UMP packets.</p>
        {error && <p className="midi-dialog-error" role="alert">{error}</p>}
        <div className="midi-monitor" aria-live="polite">
            {!messages.length && <p className="hint">Waiting for MIDI…</p>}
            {messages.map(message => <div className="midi-monitor-message" key={message.sequence}>
                <time>{new Date(message.unixMs).toLocaleTimeString()}</time>
                <code>{formatMidiWords(message.words)}</code>
            </div>)}
        </div>
    </Modal>;
}

export default function MidiWorkspace({ api = typeof window !== 'undefined' ? window.vasio : undefined,
    storage = typeof window !== 'undefined' ? window.localStorage : undefined, now: fixedNow }) {
    const [midi, setMidi] = useState(null);
    const [error, setError] = useState('');
    const [busy, setBusy] = useState(false);
    const [observations, setObservations] = useState({});
    const [live, setLive] = useState({});  // engineClock and referenceClock from supervision events
    const [widgets, setWidgets] = useState(() => loadWidgets(storage));
    const [layout, setLayout] = useState(() => loadLayout(storage));
    const [tick, setTick] = useState(() => Date.now());
    const [menu, setMenu] = useState(null);
    const [dialog, setDialog] = useState(null);
    const graphRef = useRef(null);
    const now = fixedNow ?? tick;

    const refresh = React.useCallback(async () => {
        if (!api?.getMidi) { setError('Launch the application to reach the MIDI patchbay.'); return; }
        try { setMidi(await api.getMidi()); setError(''); }
        catch (failure) { setError(failure.message || 'The MIDI patchbay is unavailable'); }
    }, [api]);

    useEffect(() => { refresh(); }, [refresh]);
    useEffect(() => {
        if (!api?.subscribeEvents) return undefined;
        return api.subscribeEvents(message => {
            if (message.event === 'supervision.observations' && message.payload?.midi) {
                setObservations(observationsByPort(message.payload.midi));
                setLive({ engineClock: message.payload.midi.engineClock, referenceClock: message.payload.midi.referenceClock });
            }
        });
    }, [api]);
    useEffect(() => {
        if (fixedNow != null) return undefined;
        const timer = window.setInterval(() => setTick(Date.now()), 250);
        return () => window.clearInterval(timer);
    }, [fixedNow]);
    useEffect(() => saveWidgets(storage, widgets), [storage, widgets]);
    useEffect(() => saveLayout(storage, layout), [storage, layout]);

    const configuration = midi?.configuration || emptyConfiguration();
    const apply = async next => {
        if (!api?.replaceMidiConfiguration) return;
        setBusy(true);
        try { setMidi(await api.replaceMidiConfiguration(next)); setError(''); }
        catch (failure) { setError(failure.message || 'The MIDI configuration was not applied'); }
        finally { setBusy(false); }
    };

    const engineClock = live.engineClock || midi?.engineClock;
    const referenceClock = live.referenceClock || midi?.referenceClock;
    const setClock = async settings => {
        if (!api?.setMidiClock) return;
        setBusy(true);
        try { setMidi(await api.setMidiClock(settings)); setLive({}); setError(''); }
        catch (failure) { setError(failure.message || 'The engine clock was not changed'); }
        finally { setBusy(false); }
    };

    const graphNodes = useMemo(() => buildMidiNodes(midi), [midi]);
    const portOwner = useMemo(() => {
        const owners = new Map();
        for (const node of graphNodes) for (const port of node.ports) owners.set(port.id, node);
        return owners;
    }, [graphNodes]);
    const portLabel = portId => {
        const node = portOwner.get(portId);
        const port = node?.ports.find(item => item.id === portId && item.side !== 'destination') ||
            node?.ports.find(item => item.id === portId);
        return node ? (node.kind === 'application' ? `${node.label} In` : `${node.label} · ${port?.name || portId}`) : portId;
    };
    const portById = useMemo(() => new Map((midi?.ports || []).map(port => [port.id, port])), [midi]);
    const statusById = routeStatusById(midi);

    const renderPort = port => {
        const observed = portObservation(port.portRef, observations);
        const active = port.side === 'source' && isActive(observed.activity?.lastUnixMs, now);
        const clock = port.side === 'source' && observed.clock?.running ? observed.clock : null;
        return <li key={`${port.side}:${port.id}`} className={`midi-port ${port.side}`}>
            {port.side === 'destination' && <Handle type="target" id={inHandle(port.id)} position={Position.Left}
                className="patchbay-flow-handle midi-handle" />}
            {port.side === 'source' && <span className={`midi-led${active ? ' on' : ''}`} role="img"
                aria-label={active ? 'Activity' : 'No recent activity'} title="Notes, controllers and other messages, not clock ticks" />}
            <span className="midi-port-name">{port.name}
                {port.portRef.kind === 'virtual' && <small>{port.side === 'source' ? 'from app' : 'to app'}</small>}</span>
            {clock && <span className={`midi-clock-badge${clock.echoSuspected ? ' echo' : ''}`}
                title={clockStateLabel(clock)}>♩ {formatBpm(clock)}</span>}
            {port.side === 'source' && <Handle type="source" id={outHandle(port.id)} position={Position.Right}
                className="patchbay-flow-handle midi-handle" />}
        </li>;
    };

    const renderEngineClock = node => <div className={`midi-node engine-clock${engineClock?.running ? ' running' : ''}`}
        data-node-id={node.id} aria-label="Engine clock">
        <div className="midi-node-heading"><strong>Engine clock</strong>
            <small>{engineClock?.mode === 'midi-clock' ? `External MIDI Clock${engineClock.locked ? ` · ${engineClock.bpm?.toFixed?.(1) || '…'} BPM` : ' · waiting for tempo'}`
                : referenceClock?.source === 'asio' ? 'Counted in sound card samples'
                    : referenceClock?.source === 'software' ? 'Counted in software clock samples' : 'Reference clock not running'}</small></div>
        <div className="midi-engine-clock-body">
            <output className="midi-engine-clock-bpm">{(engineClock?.bpm ?? 120).toFixed(1)}</output> BPM
            <span className="midi-engine-clock-state">{engineClock?.waitingForReferenceClock ? 'Waiting for the reference clock'
                : engineClock?.running ? 'Running' : 'Stopped'}</span>
        </div>
        <ul>{node.ports.map(renderPort)}</ul>
    </div>;

    const renderNode = node => node.kind === 'engineClock' ? renderEngineClock(node) : <div className={`midi-node ${node.kind}${node.role ? ` ${node.role}` : ''}${node.offline ? ' offline' : ''}`}
        data-node-id={node.id} aria-label={`${node.kind === 'application' ? 'MIDI loopback' : 'MIDI device'} ${node.label}`}>
        <div className="midi-node-heading"><strong>{node.label}</strong>
            <small>{node.role === 'loopback' ? 'Loopback · Windows MIDI Services' : node.offline ? 'Device · offline' : formatEndpointSummary(node.ports[0]?.portRef)}</small></div>
        <ul>{node.ports.map(renderPort)}</ul>
    </div>;

    const renderWidget = widget => {
        const port = portById.get(widget.portId);
        const clock = port ? portObservation(port, observations).clock : null;
        const beat = clock?.running && isActive(clock.lastTickUnixMs, now);
        return <div className={`midi-bpm-widget${clock?.running ? ' running' : ''}${clock?.echoSuspected ? ' echo' : ''}`}
            data-widget-id={widget.id} aria-label="BPM display">
            <Handle type="target" id={WIDGET_HANDLE} position={Position.Left} className="patchbay-flow-handle midi-handle" />
            <div className="midi-bpm-heading"><span className={`midi-led clock${beat ? ' on' : ''}`} aria-hidden="true" />BPM</div>
            <output className="midi-bpm-value" aria-live="off">{port ? formatBpm(clock) : '—'}</output>
            <div className="midi-bpm-port">{widget.portId ? portLabel(widget.portId) : 'Not assigned'}</div>
            <div className="midi-bpm-state">{port ? clockStateLabel(clock) : widget.portId ? 'Port not available' : 'Right-click to assign a port'}
                {clock?.transport && <span className="midi-transport"> · {clock.transport}</span>}</div>
        </div>;
    };

    const flowNodes = useMemo(() => [
        ...graphNodes.map((node, index) => ({ id: node.id, type: 'midi',
            position: layout[node.id] || defaultPosition(node, index), data: { render: () => renderNode(node) } })),
        ...widgets.map((widget, index) => ({ id: widgetNodeId(widget.id), type: 'midi',
            position: layout[widgetNodeId(widget.id)] || defaultPosition({ kind: 'widget' }, index),
            data: { render: () => renderWidget(widget) } }))
    // render closures read the latest observations through `now`
    // eslint-disable-next-line react-hooks/exhaustive-deps
    ], [graphNodes, widgets, layout, observations, now, portById, engineClock, referenceClock]);

    const flowEdges = [
        ...configuration.routes.map(route => {
            const from = portOwner.get(route.source), to = portOwner.get(route.destination);
            if (!from || !to) return null;
            const active = !!statusById[route.id]?.active;
            const routeStatus = statusById[route.id];
            const channelClass = routeChannelClass(route, routeStatus);
            const flowing = active && isActive(portObservation(portById.get(route.source) || {}, observations)
                .activity?.lastUnixMs, now);
            return { id: `route:${route.id}`, source: from.id, target: to.id,
                sourceHandle: outHandle(route.source), targetHandle: inHandle(route.destination),
                className: `midi-route-edge${active ? '' : ' inactive'}${route.reclock ? ' reclock' : ''}${channelClass ? ` ${channelClass}` : ''}`, animated: flowing,
                label: formatRouteStatus(route, routeStatus), labelShowBg: true,
                labelBgPadding: [6, 3], labelBgBorderRadius: 4,
                ariaLabel: `${portLabel(route.source)} to ${route.destination}${routeStatus?.dominantChannel ? `, dominant channel ${routeStatus.dominantChannel}` : ''}${route.reclock ? ', clock reclocked on the reference clock' : ''}${active ? '' : ', inactive'}` };
        }).filter(Boolean),
        ...widgets.filter(widget => portOwner.has(widget.portId)).map(widget => ({
            id: `widget:${widget.id}`, source: portOwner.get(widget.portId).id, target: widgetNodeId(widget.id),
            sourceHandle: outHandle(widget.portId), targetHandle: WIDGET_HANDLE, className: 'midi-monitor-edge',
            ariaLabel: `BPM display observes ${portLabel(widget.portId)}` }))
    ];

    const isValidConnection = connection =>
        connection.sourceHandle?.startsWith('out:') && connection.targetHandle?.startsWith('in:') &&
        (connection.targetHandle === WIDGET_HANDLE || connection.sourceHandle.slice(4) !== connection.targetHandle.slice(3));
    const assignWidget = (widgetId, portId) =>
        setWidgets(current => current.map(widget => widget.id === widgetId ? { ...widget, portId } : widget));
    const onConnect = connection => {
        if (!isValidConnection(connection) || busy) return;
        const source = connection.sourceHandle.slice(4);
        if (connection.targetHandle === WIDGET_HANDLE) {
            assignWidget(connection.target.slice('bpm:'.length), source);
            return;
        }
        apply(withRoute(configuration, source, connection.targetHandle.slice(3)));
    };

    // The menu is fixed-positioned, like the audio patchbay menu.
    const openMenu = (event, kind, id) => setMenu({ kind, id, x: event.clientX ?? 24, y: event.clientY ?? 24 });
    const menuAction = action => { setMenu(null); action(); };
    const widgetChoices = sourcePorts(midi).map(port => {
        const clock = portObservation(port, observations).clock;
        return { id: port.id, label: `${portLabel(port.id)}${clock?.running ? ` · clock ${formatBpm(clock)} BPM` : ''}` };
    });
    const addWidget = portId => setDialog({ kind: 'widget', widget: null,
        suggested: portId || suggestedClockPort(midi, observations) });

    const contextNode = menu?.kind === 'node' ? graphNodes.find(node => node.id === menu.id) : null;
    const contextWidget = menu?.kind === 'node' && menu.id.startsWith('bpm:')
        ? widgets.find(widget => widgetNodeId(widget.id) === menu.id) : null;
    const contextRoute = menu?.kind === 'edge' && menu.id.startsWith('route:')
        ? configuration.routes.find(route => `route:${route.id}` === menu.id) : null;
    const contextWidgetEdge = menu?.kind === 'edge' && menu.id.startsWith('widget:')
        ? widgets.find(widget => `widget:${widget.id}` === menu.id) : null;
    const contextVirtual = contextNode?.kind === 'application'
        ? configuration.virtualPorts.find(port => port.id === contextNode.resourceId) : null;
    const contextPort = contextVirtual ? portById.get(`midi:virtual:${contextVirtual.id}`) : null;
    const contextMonitorPort = contextPort || contextNode?.ports.find(port => port.side === 'source')?.portRef || null;
    const firstSource = node => node?.ports.find(port => port.side === 'source')?.id;

    const clockSources = Object.values(observations).filter(port => port.clock?.running);
    const unavailable = midi && !midi.available;
    const unavailableMessage = unavailable
        ? `Windows MIDI Services (Windows.Devices.Midi2) is not available on this machine. This version has no legacy MIDI fallback.${midi.error ? ` ${midi.error}` : ''}`
        : '';

    return <section className="section midi-workspace" aria-label="MIDI patchbay">
        <div className="workspace-heading"><div><h2>MIDI</h2>
            <p>MIDI 1.0 endpoints through Windows MIDI Services. Each application gets its own In and Out ports; the engine observes all traffic.</p></div>
            <div className="midi-status" role="status">
                {busy ? 'Applying…' : unavailable ? `MIDI unavailable · ${unavailableMessage}` :
                    midi ? `${graphNodes.length} blocks · ${configuration.routes.length} connections` +
                        (clockSources.length ? ` · clock from ${clockSources.map(port => portLabel(port.portId)).join(', ')}` : '') : 'Loading…'}
                {referenceClock && <div className="midi-reference-clock">{referenceClockLabel(referenceClock)}</div>}
            </div></div>
        {error && <p className="notice midi-error" role="alert">{error}</p>}
        <div className="patchbay-flow midi-flow" role="region" aria-label="MIDI connections" ref={graphRef} tabIndex="0"
            onContextMenu={event => { if (event.defaultPrevented) return; event.preventDefault(); openMenu(event, 'pane'); }}
            onKeyDown={event => {
                if (event.key === 'Escape') setMenu(null);
                if (event.key === 'ContextMenu' || (event.shiftKey && event.key === 'F10')) {
                    event.preventDefault(); setMenu({ kind: 'pane', x: 24, y: 24 });
                }
            }}>
            <MidiCanvas modelNodes={flowNodes} edges={flowEdges} isValidConnection={isValidConnection} onConnect={onConnect}
                onSavePosition={(id, position) => setLayout(current => ({ ...current, [id]: { x: position.x, y: position.y } }))}
                onPaneContextMenu={event => openMenu(event, 'pane')}
                onNodeContextMenu={(event, id) => openMenu(event, 'node', id)}
                onEdgeContextMenu={(event, id) => openMenu(event, 'edge', id)} />
            {midi && !graphNodes.length && !widgets.length && <div className="patchbay-flow-empty" role="status">
                {unavailable ? <><strong>MIDI is unavailable</strong><span>{unavailableMessage}</span></> :
                    <><strong>No MIDI port yet</strong><span>Right-click the graph to add a MIDI loopback or a BPM display.</span></>}
            </div>}
        </div>
        {menu && createPortal(<div className="patchbay-context-menu" role="menu" aria-label="MIDI actions"
            style={{ left: Math.max(8, menu.x), top: Math.max(8, menu.y) }}
            onPointerDown={event => event.stopPropagation()}
            onKeyDown={event => { if (event.key === 'Escape') setMenu(null); }}>
            {menu.kind === 'pane' && <>
                <button type="button" role="menuitem" disabled={busy || unavailable}
                    onClick={() => menuAction(() => setDialog({ kind: 'port', port: null }))}>Add MIDI loopback…</button>
                <button type="button" role="menuitem" onClick={() => menuAction(() => addWidget())}>Add BPM display…</button>
                <button type="button" role="menuitem" onClick={() => menuAction(refresh)}>Refresh MIDI ports</button>
            </>}
            {contextNode && <>
                <strong>{contextNode.label}</strong>
                {contextNode.kind === 'engineClock' && <>
                    <button type="button" role="menuitem" disabled={busy || engineClock?.running || !referenceClock || referenceClock.source === 'none'}
                        title={referenceClock?.source === 'none' ? 'Start audio first: the clock counts the reference clock samples' : undefined}
                        onClick={() => menuAction(() => setClock({ transport: 'start' }))}>Start clock</button>
                    <button type="button" role="menuitem" disabled={busy || engineClock?.running || !referenceClock || referenceClock.source === 'none'}
                        onClick={() => menuAction(() => setClock({ transport: 'continue' }))}>Continue clock</button>
                    <button type="button" role="menuitem" disabled={busy || !engineClock?.running}
                        onClick={() => menuAction(() => setClock({ transport: 'stop' }))}>Stop clock</button>
                    <button type="button" role="menuitem" disabled={busy}
                        onClick={() => menuAction(() => setDialog({ kind: 'tempo' }))}>Set tempo…</button>
                    <button type="button" role="menuitem" disabled={busy}
                        onClick={() => menuAction(() => setDialog({ kind: 'clock-source' }))}>MIDI clock source…</button>
                    <button type="button" role="menuitem" disabled={busy}
                        onClick={() => menuAction(() => setDialog({ kind: 'midi-learn' }))}>MIDI Learn clock…</button>
                </>}
                {contextVirtual && <button type="button" role="menuitem" disabled={busy}
                    onClick={() => menuAction(() => setDialog({ kind: 'port', port: contextVirtual }))}>Edit MIDI loopback…</button>}
                {contextMonitorPort && <button type="button" role="menuitem"
                    onClick={() => menuAction(() => setDialog({ kind: 'monitor', port: contextMonitorPort }))}>Monitor incoming MIDI…</button>}
                {firstSource(contextNode) && <button type="button" role="menuitem"
                    onClick={() => menuAction(() => addWidget(firstSource(contextNode)))}>Add BPM display for this block…</button>}
                {contextVirtual && <button type="button" role="menuitem" disabled={busy}
                    onClick={() => menuAction(() => apply(withoutApplicationPort(configuration, contextVirtual.id)))}>Remove MIDI loopback</button>}
            </>}
            {contextWidget && <>
                <strong>BPM display</strong>
                <button type="button" role="menuitem"
                    onClick={() => menuAction(() => setDialog({ kind: 'widget', widget: contextWidget }))}>Assign to a port…</button>
                <button type="button" role="menuitem" onClick={() => menuAction(() =>
                    setWidgets(current => current.filter(widget => widget.id !== contextWidget.id)))}>Remove BPM display</button>
            </>}
            {contextRoute && <>
                <button type="button" role="menuitem" disabled={busy}
                    onClick={() => menuAction(() => setDialog({ kind: 'route', route: contextRoute }))}>Connection properties…</button>
                <button type="button" role="menuitem" disabled={busy}
                    onClick={() => menuAction(() => apply(withoutRoute(configuration, contextRoute.id)))}>Delete connection</button>
            </>}
            {contextWidgetEdge && <button type="button" role="menuitem"
                onClick={() => menuAction(() => assignWidget(contextWidgetEdge.id, ''))}>Unassign BPM display</button>}
        </div>, document.body)}
        {dialog?.kind === 'port' && <ApplicationPortDialog port={dialog.port}
            takenIds={new Set(configuration.virtualPorts.map(port => port.id))}
            onClose={() => setDialog(null)}
            onSave={port => { setDialog(null); apply(withApplicationPort(configuration, port)); }} />}
        {dialog?.kind === 'route' && <RouteDialog route={dialog.route}
            title={`${portLabel(dialog.route.source)} → ${dialog.route.destination.startsWith(VIRTUAL_PREFIX)
                ? `${portOwner.get(dialog.route.destination)?.label || dialog.route.destination} Out`
                : portLabel(dialog.route.destination)}`}
            onClose={() => setDialog(null)}
            onDelete={() => { setDialog(null); apply(withoutRoute(configuration, dialog.route.id)); }}
            onSave={route => { setDialog(null); apply(withUpdatedRoute(configuration, route)); }} />}
        {dialog?.kind === 'tempo' && <TempoDialog bpm={engineClock?.bpm} onClose={() => setDialog(null)}
            onSave={bpm => { setDialog(null); setClock({ bpm }); }} />}
        {dialog?.kind === 'clock-source' && <ClockSourceDialog ports={midi?.ports || []} source={engineClock?.source}
            onClose={() => setDialog(null)}
            onSave={source => { setDialog(null); setClock({ source }); }} />}
        {dialog?.kind === 'midi-learn' && <MidiLearnDialog api={api} engineClock={engineClock} ports={midi?.ports || []}
            onChanged={next => setMidi(next)} onClose={() => setDialog(null)} />}
        {dialog?.kind === 'widget' && <WidgetDialog widget={dialog.widget} choices={widgetChoices} suggested={dialog.suggested}
            onClose={() => setDialog(null)}
            onSave={portId => {
                const target = dialog.widget;
                setDialog(null);
                if (target) assignWidget(target.id, portId);
                else setWidgets(current => [...current, { id: `w${Date.now().toString(36)}${current.length}`, portId }]);
            }} />}
        {dialog?.kind === 'monitor' && <MidiMonitorDialog api={api} port={dialog.port}
            baselineSequence={Math.max(0, ...(dialog.port.recentMessages || []).map(message => message.sequence))}
            onClose={() => setDialog(null)} />}
    </section>;
}
