import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import './App.css';
import './ProfessionalTheme.css';
import RoutingPatchbay from './RoutingPatchbay';
import NativeAudioDialog from './NativeAudioDialog';
import QuickRouting from './QuickRouting';
import BufferTimingDiagram from './BufferTimingDiagram';
import AnalysisTimeline from './AnalysisTimeline';
import { readHardwareTransfer, saveHardwareTransfer, transferRoutesToDriver } from './hardware-transfer';
import { DEFAULT_ROUTE_COLOR, readRouteVisuals, routeVisualKey, saveRouteVisuals } from './route-visuals';
import { transferProcessConnections } from './process-connection-transfer';
const { configurationFromState } = require('./api-contract');
const { mergeConfigurationDraft } = require('./configuration-merge');
const openApiDocument = require('./openapi-spec.generated');
const migrationLogKey = 'timoxvasio.connection-migrations.v1';
const readMigrationLog = () => {
    try { return JSON.parse(window.localStorage.getItem(migrationLogKey)) || []; }
    catch { return []; }
};

function App() {
    const [connectionError, setConnectionError] = useState('Connecting to engine…');
    const [apiState, setApiState] = useState(null);
    const connectionGeneration = useRef(0);
    const [configuration, setConfiguration] = useState(null);
    const [engine, setEngine] = useState(null);
    const softwareClock = configuration?.clockSource?.kind === 'software';
    const runningSoftwareClock = engine?.clockSource?.kind === 'software';
    const invalidSoftwareFormat = softwareClock && (!Number.isInteger(configuration.sampleRate) ||
        configuration.sampleRate < 8000 || configuration.sampleRate > 384000 ||
        !Number.isInteger(configuration.bufferFrames) || configuration.bufferFrames < 16 || configuration.bufferFrames > 8192);
    const [applicationProfiles, setApplicationProfiles] = useState(null);
    const [confirmedProfiles, setConfirmedProfiles] = useState([]);
    const [profilesLoading, setProfilesLoading] = useState(true);
    const [profilesSaving, setProfilesSaving] = useState(false);
    const [profilesError, setProfilesError] = useState('');
    const [profilesNotice, setProfilesNotice] = useState('');
    const [notice, setNotice] = useState('');
    const [applying, setApplying] = useState(false);
    const [selectedRouteId, setSelectedRouteId] = useState(null);
    const [routingDialog, setRoutingDialog] = useState(null);
    const [bridgeDialog, setBridgeDialog] = useState(null);
    const [nativeDialogOpen, setNativeDialogOpen] = useState(false);
    const [bridgeInventoryBusy, setBridgeInventoryBusy] = useState(false);
    const [migrationLog, setMigrationLog] = useState(readMigrationLog);
    const pendingMigrations = useRef([]);
    const [routeVisuals, setRouteVisuals] = useState(readRouteVisuals);
    const [hardwareTransfer, setHardwareTransfer] = useState(readHardwareTransfer);
    const [activeView, setActiveView] = useState('configuration');
    const [diagnostics, setDiagnostics] = useState({ level: 'info', entries: [] });
    const [diagnosticsError, setDiagnosticsError] = useState('');
    const [diagnosticsLoading, setDiagnosticsLoading] = useState(false);
    const [engineAction, setEngineAction] = useState('');
    const [meters, setMeters] = useState({});
    const [transportHistory, setTransportHistory] = useState([]);
    const [performanceHistory, setPerformanceHistory] = useState([]);
    const [selectedStereoPairId, setSelectedStereoPairId] = useState('');
    const [correlation, setCorrelation] = useState({ state: 'stopped', correlation: null });
    const [correlationHistory, setCorrelationHistory] = useState([]);
    const [apiEvents, setApiEvents] = useState([]);
    const [consoleMode, setConsoleMode] = useState('websocket');
    const [consoleCommand, setConsoleCommand] = useState('engine.start');
    const [consoleRequest, setConsoleRequest] = useState(() => JSON.stringify({
        id: `engine-start-${Date.now()}`, command: 'engine.start'
    }, null, 2));
    const [consoleResult, setConsoleResult] = useState('');
    const [consoleBusy, setConsoleBusy] = useState(false);
    const [measurementsBusy, setMeasurementsBusy] = useState(false);
    const [measurementsError, setMeasurementsError] = useState('');
    const [consoleConfirmation, setConsoleConfirmation] = useState('');
    const confirmationDialog = useRef(null);
    const metersRef = useRef({});
    const lastConfirmedConfiguration = useRef(null);
    const draftBaseConfiguration = useRef(null);

    useEffect(() => { saveRouteVisuals(routeVisuals); }, [routeVisuals]);
    useEffect(() => { saveHardwareTransfer(hardwareTransfer); }, [hardwareTransfer]);
    useEffect(() => {
        try { window.localStorage.setItem(migrationLogKey, JSON.stringify(migrationLog)); }
        catch { /* Local storage may be disabled. */ }
    }, [migrationLog]);
    useEffect(() => {
        if (!routingDialog) return undefined;
        const closeOnEscape = event => { if (event.key === 'Escape') setRoutingDialog(null); };
        document.addEventListener('keydown', closeOnEscape);
        return () => document.removeEventListener('keydown', closeOnEscape);
    }, [routingDialog]);

    const updateManyRouteVisuals = (routes, changes) => setRouteVisuals(current => {
        const next = { ...current };
        for (const route of routes) {
            const key = routeVisualKey(route);
            next[key] = { label: '', color: DEFAULT_ROUTE_COLOR, ...current[key], ...changes };
        }
        return next;
    });
    const updateRouteVisual = (route, field, value) => updateManyRouteVisuals([route], {
        [field]: field === 'label' ? value.slice(0, 60) : value
    });

    useEffect(() => {
        const dialog = confirmationDialog.current;
        if (!dialog) return;
        if (consoleConfirmation && !dialog.open) {
            dialog.showModal();
            dialog.querySelector('button')?.focus();
        } else if (!consoleConfirmation && dialog.open) dialog.close();
    }, [consoleConfirmation]);

    const refreshDiagnostics = useCallback(async () => {
        setDiagnosticsLoading(true);
        setDiagnosticsError('');
        try { setDiagnostics(await window.vasio.getDiagnostics(300)); }
        catch (error) { setDiagnosticsError(error.message); }
        finally { setDiagnosticsLoading(false); }
    }, []);

    const setDiagnosticLevel = async level => {
        try { await window.vasio.setDiagnosticLevel(level); await refreshDiagnostics(); }
        catch (error) { setDiagnosticsError(error.message); }
    };

    const manageEngine = async action => {
        setEngineAction(action);
        setDiagnosticsError('');
        try {
            if (action === 'start') await window.vasio.startAudio();
            else if (action === 'reset') await window.vasio.executeCommand({
                id: `engine-reset-${Date.now()}`, command: 'engine.reset'
            });
            else await window.vasio.stopAudio();
            await new Promise(resolve => setTimeout(resolve, 150));
            await refreshState();
            if (action === 'reset') setNotice(`${runningSoftwareClock ? 'Software clock restarted' : 'Physical driver reopened'} with the last saved connections. Pending edits were not applied.`);
        } catch (error) { setDiagnosticsError(error.message); setNotice(error.message); }
        finally { setEngineAction(''); }
    };

    const recordPerformanceState = useCallback(state => {
        const window = state.performanceMeasurements?.enabled ? state.performanceMeasurements.window : null;
        const routedPids = new Set((state.routes || []).flatMap(route =>
            [route.sourceEndpointId, route.destinationEndpointId].map(id =>
                /^virtual:TimoxVasio:(\d+):/.exec(id)?.[1]).filter(Boolean).map(Number)));
        const clients = (state.virtualDrivers?.flatMap(driver => driver.clients || []) || [])
            .filter(client => routedPids.has(client.pid));
        if (!state.bufferHealth && !window && !clients.some(client => typeof client.processCpuPercent === 'number')) return;
        setPerformanceHistory(current => [...current, {
            time: Date.now(),
            sampleRate: state.engine?.sampleRate,
            bufferFrames: state.engine?.bufferFrames,
            bufferHealth: state.bufferHealth,
            clients: clients.map(client => ({ pid: client.pid, processName: client.processName,
                cpuPercent: client.processCpuPercent ?? null,
                callbackWorstNs: window?.clients?.find(item => item.processId === client.pid)?.hostCallback?.worstNs ?? null }))
        }].slice(-1800));
    }, []);

    const refreshState = useCallback(async ({ resetDraft = false } = {}) => {
        const generation = connectionGeneration.current;
        const state = await window.vasio.getState();
        if (generation !== connectionGeneration.current) return;
        recordPerformanceState(state);
        const confirmedConfiguration = configurationFromState(state);
        const previousConfirmed = lastConfirmedConfiguration.current;
        setApiState(state);
        setEngine(state.engine);
        setConfiguration(current => {
            if (resetDraft || !current || !previousConfirmed ||
                JSON.stringify(current) === JSON.stringify(previousConfirmed)) {
                draftBaseConfiguration.current = confirmedConfiguration;
                return confirmedConfiguration;
            }
            return current;
        });
        lastConfirmedConfiguration.current = confirmedConfiguration;
        setSelectedStereoPairId(current => state.stereoPairs?.some(pair => pair.id === current) ? current : '');
        return state;
    }, [recordPerformanceState]);

    const refreshApplicationProfiles = useCallback(async () => {
        setProfilesLoading(true);
        setProfilesError('');
        try {
            const result = await window.vasio.getApplicationProfiles();
            setApplicationProfiles(result.profiles);
            setConfirmedProfiles(result.profiles);
        } catch (error) {
            setProfilesError(error.status === 404
                ? 'The running engine does not support application profiles yet. Rebuild and restart TimoxVasio Engine.'
                : error.message);
        } finally {
            setProfilesLoading(false);
        }
    }, []);

    useEffect(() => {
        let disposed = false;
        let disconnectListener;
        let diagnosticListener;
        if (!window.vasio || typeof window.vasio.subscribeEvents !== 'function') {
            setConnectionError('Launch Timox VASIO Control to access the audio engine.');
            return undefined;
        }
        const unsubscribeEvents = window.vasio.subscribeEvents(message => {
            if (['engine.status', 'devices.changed', 'routes.changed', 'engine.error', 'audio.meter', 'audio.correlation', 'audio.stereoPairs.changed'].includes(message.event))
                setApiEvents(current => [...current, { time: new Date().toISOString(), event: message.event, payload: message.payload }].slice(-500));
            if (message.event === 'engine.status') {
                setEngine(message.payload);
                if (message.payload.state !== 'running') { metersRef.current = {}; setMeters({}); }
            }
            if (message.event === 'devices.changed') {
                metersRef.current = {};
                setMeters({});
                setApiState(current => current && ({ ...current, ...message.payload }));
            }
            if (message.event === 'routes.changed') {
                refreshState().catch(error => setConnectionError(error.message));
                metersRef.current = {};
                setMeters({});
            }
            if (message.event === 'wasapi.bridges.changed')
                setApiState(current => current && ({ ...current, wasapiBridges: message.payload.wasapiBridges }));
            if (message.event === 'engine.error') setNotice(message.payload.message);
            if (message.event === 'audio.meter') {
                metersRef.current[message.payload.endpointId] = message.payload;
                setMeters(current => ({ ...current, [message.payload.endpointId]: message.payload }));
            }
            if (message.event === 'audio.stereoPairs.changed')
                setApiState(current => current && ({ ...current, stereoPairs: message.payload.stereoPairs }));
            if (message.event === 'audio.correlation') {
                setCorrelation(message.payload);
                setCorrelationHistory(current => message.payload.state === 'stopped' ? [] :
                    message.payload.correlation == null ? current : [...current, message.payload.correlation].slice(-40));
            }
        });
        const connect = async () => {
            try {
                await Promise.all([refreshState(), refreshApplicationProfiles()]);
                if (disposed) return;
                setConnectionError('');
                disconnectListener = window.electronAPI.onEngineConnection(status => {
                    const generation = ++connectionGeneration.current;
                    if (!status.connected) setConnectionError(status.error || 'The VASIO engine is stopped');
                    else refreshState().then(() => {
                        if (!disposed && generation === connectionGeneration.current) setConnectionError('');
                    }).catch(error => {
                        if (!disposed && generation === connectionGeneration.current) setConnectionError(error.message);
                    });
                });
                diagnosticListener = window.electronAPI.onEngineDiagnostic(message => {
                    setNotice(message);
                    setApiEvents(current => [...current, { time: new Date().toISOString(), event: 'engine.diagnostic', payload: { message } }].slice(-500));
                });
                const unsubscribeDisconnect = window.vasio.onDisconnect(() => {
                    ++connectionGeneration.current;
                    if (!disposed) setConnectionError('API connection lost');
                });
                disconnectListener = (() => {
                    const removeEngineListener = disconnectListener;
                    return () => { removeEngineListener(); unsubscribeDisconnect(); };
                })();
            } catch (error) {
                if (!disposed) setConnectionError(error.message);
            }
        };
        connect();
        return () => {
            disposed = true;
            unsubscribeEvents();
            if (disconnectListener) disconnectListener();
            if (diagnosticListener) diagnosticListener();
        };
    }, [refreshApplicationProfiles, refreshState]);

    useEffect(() => {
        if (!window.vasio?.getState) return undefined;
        const timer = window.setInterval(() => {
            const generation = connectionGeneration.current;
            window.vasio.getState().then(state => {
                if (generation !== connectionGeneration.current) return;
                recordPerformanceState(state);
                setApiState(state);
                setEngine(state.engine);
                setConnectionError('');
            }).catch(error => { if (generation === connectionGeneration.current) setConnectionError(error.message); });
        }, 5000);
        return () => window.clearInterval(timer);
    }, [recordPerformanceState]);

    useEffect(() => {
        const timer = window.setInterval(() => {
            const time = Date.now();
            const directions = new Map();
            Object.values(metersRef.current).forEach(meter => {
                const match = /^virtual:TimoxVasio:(\d+):(input|output):/.exec(meter.endpointId);
                if (match && !directions.has(`${match[1]}:${match[2]}`)) directions.set(`${match[1]}:${match[2]}`, {
                    time, key: `${match[1]}:${match[2]}`, pid: Number(match[1]), direction: match[2],
                    underruns: meter.underruns, overruns: meter.overruns
                });
            });
            if (directions.size) setTransportHistory(current => [...current, ...directions.values()].slice(-7200));
        }, 5000);
        return () => window.clearInterval(timer);
    }, []);

    const endpoints = useMemo(() => {
        if (!apiState) return { all: [], sourceGroups: [], destinationGroups: [] };
        const physical = softwareClock ? [] : apiState.physicalDrivers;
        const virtual = apiState.virtualDrivers;
        const physicalInputs = physical.flatMap(driver => driver.inputEndpoints);
        const physicalOutputs = physical.flatMap(driver => driver.outputEndpoints);
        const profiled = confirmedProfiles.flatMap(profile => {
            const name = profile.processName.trim().toLowerCase();
            const ports = (direction, count) => Array.from({ length: Math.max(0, Math.min(256, count)) }, (_, index) => ({
                id: `virtual:TimoxVasio:app:${name}:${direction}:${index + 1}`,
                name: `TimoxVasio ${profile.processName} ${direction === 'input' ? 'In' : 'Out'} ${index + 1} (profile)`,
                direction, channel: index + 1
            }));
            return [{ processName: profile.processName, direction: 'input', endpoints: ports('input', profile.inputChannels) },
                { processName: profile.processName, direction: 'output', endpoints: ports('output', profile.outputChannels) }];
        });
        const virtualInputs = [...virtual.flatMap(driver => driver.inputEndpoints),
            ...profiled.filter(group => group.direction === 'input').flatMap(group => group.endpoints)];
        const virtualOutputs = [...virtual.flatMap(driver => driver.outputEndpoints),
            ...profiled.filter(group => group.direction === 'output').flatMap(group => group.endpoints)];
        return {
            all: [...physical.flatMap(driver => [...driver.inputEndpoints, ...driver.outputEndpoints]),
                ...virtualInputs, ...virtualOutputs],
            profiled,
            sourceGroups: [
                { id: 'physical-input', label: 'Physical inputs', endpoints: physicalInputs },
                { id: 'physical-output', label: 'Physical outputs', endpoints: physicalOutputs },
                { id: 'virtual-input', label: 'Virtual inputs', endpoints: virtualInputs },
                { id: 'virtual-output', label: 'Virtual outputs', endpoints: virtualOutputs }
            ],
            destinationGroups: [
                { id: 'physical-input', label: 'Physical inputs', endpoints: physicalInputs },
                { id: 'physical-output', label: 'Physical outputs', endpoints: physicalOutputs },
                { id: 'virtual-input', label: 'Virtual inputs', endpoints: virtualInputs },
                { id: 'virtual-output', label: 'Virtual outputs', endpoints: virtualOutputs }
            ]
        };
    }, [apiState, confirmedProfiles, softwareClock]);
    const bridgeGroups = useMemo(() => (configuration?.wasapiBridges || []).map(bridge => {
        const runtime = apiState?.wasapiBridges?.find(item => item.id === bridge.id);
        const capture = bridge.kind !== 'render';
        const direction = capture ? 'input' : 'output';
        const prefix = `wasapi:${bridge.id}:${direction}:`;
        const ports = new Map((runtime?.[capture ? 'inputEndpoints' : 'outputEndpoints'] || []).map(port => [port.id, port]));
        for (const route of configuration.routes) {
            const id = capture ? route.sourceEndpointId : route.destinationEndpointId;
            if (id.startsWith(prefix) && !ports.has(id)) ports.set(id, { id, name: `${bridge.id} ${direction} ${id.slice(prefix.length)} (saved)`,
                direction, channel: Number(id.slice(prefix.length)) });
        }
        return { id: `wasapi:${bridge.id}`, bridgeId: bridge.id, kind: capture ? 'wasapi-capture' : 'wasapi-render',
            label: `${bridge.id} · ${bridge.enabled === false ? 'disabled' : runtime?.state || 'pending'}`,
            bridgeKind: bridge.kind, enabled: bridge.enabled !== false,
            resolvedEndpointId: runtime?.effectiveInfo?.endpointId || bridge.endpointId || null,
            endpoints: [...ports.values()] };
    }), [configuration, apiState]);
    const bridgeConnectionAllowed = (sourceId, destinationId) => {
        const source = bridgeGroups.find(group => group.endpoints.some(port => port.id === sourceId));
        const destination = bridgeGroups.find(group => group.endpoints.some(port => port.id === destinationId));
        return source?.enabled !== false && destination?.enabled !== false &&
            !(source?.bridgeKind === 'endpointLoopback' && destination?.bridgeKind === 'render' &&
                source.resolvedEndpointId && source.resolvedEndpointId === destination.resolvedEndpointId);
    };
    const refreshBridgeInventory = async () => {
        if (!window.vasio.getWasapiDevices) return;
        setBridgeInventoryBusy(true);
        try { const inventory = await window.vasio.getWasapiDevices();
            setApiState(current => ({ ...current, ...inventory }));
        } catch (error) { setNotice(error.message); }
        finally { setBridgeInventoryBusy(false); }
    };
    const patchbayGroups = useMemo(() => {
        if (!apiState) return { sources: [], destinations: [] };
        const physical = softwareClock ? [] : apiState.physicalDrivers || [];
        const virtual = apiState.virtualDrivers || [];
        const virtualGroups = direction => virtual.flatMap(driver => (driver.clients || []).map(client => {
            const prefix = `virtual:${driver.id}:${client.pid}:${direction}:`;
            return {
                id: `${driver.id}:${client.pid}:${direction}`,
                label: `${client.processName} · PID ${client.pid}`,
                kind: direction === 'output' ? 'virtual-output' : 'virtual-input',
                endpoints: (direction === 'output' ? driver.outputEndpoints : driver.inputEndpoints)
                    .filter(endpoint => endpoint.id.startsWith(prefix))
            };
        })).filter(group => group.endpoints.length);
        const profileGroups = direction => endpoints.profiled.filter(group =>
            group.direction === direction && group.endpoints.length).map(group => ({
            id: `profile:${group.processName}:${direction}`,
            label: `${group.processName} · Saved profile (available when channels open)`,
            kind: direction === 'output' ? 'virtual-output' : 'virtual-input',
            endpoints: group.endpoints
        }));
        const savedGroups = direction => {
            const byName = new Map();
            const known = new Set(endpoints.profiled.filter(group => group.direction === direction)
                .flatMap(group => group.endpoints.map(endpoint => endpoint.id)));
            for (const route of configuration?.routes || []) {
                const id = direction === 'output' ? route.sourceEndpointId : route.destinationEndpointId;
                const match = /^virtual:TimoxVasio:app:(.+):(input|output):(\d+)$/.exec(id);
                if (!match || match[2] !== direction || known.has(id)) continue;
                if (!byName.has(match[1])) byName.set(match[1], new Map());
                byName.get(match[1]).set(id, { id, name: `${match[1]} ${direction === 'input' ? 'In' : 'Out'} ${match[3]}`,
                    direction, channel: Number(match[3]) });
            }
            return [...byName].map(([name, ports]) => ({
                id: `saved:${name}:${direction}`,
                label: `${name} · Process not open`,
                kind: direction === 'output' ? 'virtual-output' : 'virtual-input',
                endpoints: [...ports.values()]
            }));
        };
        // Saved routes use application names, while live inventory uses PIDs.
        // Resolve only an unambiguous open channel, retaining the durable ID for edits.
        const applicationGroups = direction => {
            const live = virtualGroups(direction);
            const fallback = [...profileGroups(direction), ...savedGroups(direction)];
            const clients = virtual.flatMap(driver => driver.clients || []);
            const referenced = new Set((configuration?.routes || []).flatMap(route =>
                [route.sourceEndpointId, route.destinationEndpointId]));
            for (const group of fallback) {
                group.endpoints = group.endpoints.filter(endpoint => {
                    const match = /^virtual:TimoxVasio:app:(.+):(input|output):(\d+)$/.exec(endpoint.id);
                    if (!match) return true;
                    const matches = clients.filter(client => client.processName.trim().toLowerCase() === match[1].toLowerCase());
                    if (matches.length !== 1) return true;
                    const runtimeId = `virtual:TimoxVasio:${matches[0].pid}:${direction}:${match[3]}`;
                    const owner = live.find(item => item.endpoints.some(port => port.id === runtimeId));
                    if (!owner) return true;
                    const runtime = owner.endpoints.find(port => port.id === runtimeId);
                    if (!referenced.has(runtimeId)) owner.endpoints = owner.endpoints.filter(port => port.id !== runtimeId);
                    owner.endpoints.push({ ...runtime, id: endpoint.id });
                    return false;
                });
            }
            return [...live, ...fallback.filter(group => group.endpoints.length)];
        };
        return {
            sources: [
                ...bridgeGroups.filter(group => group.kind === 'wasapi-capture'),
                ...applicationGroups('output'),
                ...physical.filter(driver => driver.inputEndpoints.length).map(driver => ({
                    id: `${driver.id}:input`, label: `${driver.name} · Inputs`, kind: 'physical-input',
                    endpoints: driver.inputEndpoints
                }))
            ],
            destinations: [
                ...bridgeGroups.filter(group => group.kind === 'wasapi-render'),
                ...physical.filter(driver => driver.outputEndpoints.length).map(driver => ({
                    id: `${driver.id}:output`, label: `${driver.name} · Outputs`, kind: 'physical-output',
                    endpoints: driver.outputEndpoints
                })),
                ...applicationGroups('input')
            ]
        };
    }, [apiState, endpoints.profiled, configuration?.routes, softwareClock, bridgeGroups]);
    const confirmedRoutes = apiState ? configurationFromState(apiState).routes : [];

    const configurationLocked = applying || engine?.state === 'reconfiguring';
    const stableEndpointId = id => {
        const match = /^virtual:TimoxVasio:(\d+):(input|output):(\d+)$/.exec(id);
        if (!match) return id;
        const client = (apiState?.virtualDrivers || []).flatMap(driver => driver.clients || [])
            .find(item => String(item.pid) === match[1]);
        return client?.processName
            ? `virtual:TimoxVasio:app:${client.processName.toLowerCase()}:${match[2]}:${match[3]}` : id;
    };
    const updateDraft = (update) => setConfiguration(current =>
        configurationLocked || !current ? current : update(current));
    const updateConfiguration = (field, value) => updateDraft(current => ({ ...current, [field]: value }));
    const toggleRoute = (sourceEndpointId, destinationEndpointId) => setConfiguration(current => {
        if (!current) return current;
        if (!bridgeConnectionAllowed(sourceEndpointId, destinationEndpointId) && !current.routes.some(route =>
            route.sourceEndpointId === sourceEndpointId && route.destinationEndpointId === destinationEndpointId)) return current;
        if (configurationLocked || !current) return current;
        sourceEndpointId = stableEndpointId(sourceEndpointId);
        destinationEndpointId = stableEndpointId(destinationEndpointId);
        const existing = current.routes.find(route => route.sourceEndpointId === sourceEndpointId &&
            route.destinationEndpointId === destinationEndpointId);
        if (existing) return { ...current, routes: current.routes.filter(route => route.id !== existing.id) };
        return { ...current, routes: [...current.routes, {
            id: `route-${Date.now()}-${current.routes.length}`,
            sourceEndpointId,
            destinationEndpointId,
            gainDb: 0,
            mute: false
        }] };
    });
    const reconnectRoute = (routeId, sourceEndpointId, destinationEndpointId) => setConfiguration(current => {
        if (!bridgeConnectionAllowed(sourceEndpointId, destinationEndpointId)) return current;
        sourceEndpointId = stableEndpointId(sourceEndpointId);
        destinationEndpointId = stableEndpointId(destinationEndpointId);
        if (configurationLocked || !current || current.routes.some(route => route.id !== routeId &&
            route.sourceEndpointId === sourceEndpointId && route.destinationEndpointId === destinationEndpointId)) return current;
        return { ...current, routes: current.routes.map(route => route.id === routeId
            ? { ...route, sourceEndpointId, destinationEndpointId } : route) };
    });
    const addRoutes = routes => updateDraft(current => ({ ...current,
        routes: [...current.routes, ...routes.filter(route => bridgeConnectionAllowed(route.sourceEndpointId, route.destinationEndpointId)).map((route, index) => ({
            id: `route-${Date.now()}-${current.routes.length + index}`,
            ...route, sourceEndpointId: stableEndpointId(route.sourceEndpointId),
            destinationEndpointId: stableEndpointId(route.destinationEndpointId),
            gainDb: 0, mute: false
        }))]
    }));
    const moveProcessConnections = (sourceName, destinationName) => {
        if (!configuration || configurationLocked) return;
        const result = transferProcessConnections(configuration.routes, sourceName, destinationName);
        if (!result.moved) return;
        const before = new Map(configuration.routes.map(route => [route.id, route]));
        pendingMigrations.current.push(...result.routes.filter(route => {
            const original = before.get(route.id);
            return original && (original.sourceEndpointId !== route.sourceEndpointId ||
                original.destinationEndpointId !== route.destinationEndpointId);
        }).map(route => ({ from: before.get(route.id), to: route })));
        updateDraft(current => ({ ...current, routes: transferProcessConnections(
            current.routes, sourceName, destinationName).routes }));
        setSelectedRouteId(null);
        setNotice(`${result.moved} connections moved from ${sourceName} to ${destinationName} in the draft` +
            `${result.duplicates ? `; ${result.duplicates} duplicates kept their existing target settings` : ''}. Review the graph, then Apply configuration.`);
    };
    const editRoute = (id, field, value) => updateDraft(current => ({
        ...current, routes: current.routes.map(route => route.id === id ? { ...route, [field]: value } : route)
    }));
    const assignProfileToProcess = async (processName, channels) => {
        if (profilesLoading || profilesSaving) throw new Error('Application profiles are busy. Try again.');
        const normalizedName = processName.toLocaleLowerCase();
        const runningPids = new Set((apiState?.virtualDrivers || []).flatMap(driver => driver.clients || [])
            .filter(client => client.processName.toLocaleLowerCase() === normalizedName).map(client => String(client.pid)));
        for (const route of configuration?.routes || []) {
            for (const id of [route.sourceEndpointId, route.destinationEndpointId]) {
                const match = /^virtual:TimoxVasio:(\d+|app:[^:]+):(input|output):(\d+)$/i.exec(id);
                if (!match || (match[1].toLocaleLowerCase() !== `app:${normalizedName}` && !runningPids.has(match[1]))) continue;
                const limit = match[2].toLocaleLowerCase() === 'input' ? channels.inputChannels : channels.outputChannels;
                if (Number(match[3]) > limit) throw new Error(`Saved connection uses ${match[2]} channel ${match[3]}. Keep at least ${match[3]} ${match[2]} channels or remove that connection first.`);
            }
        }
        setProfilesSaving(true);
        setProfilesError('');
        setProfilesNotice('');
        try {
            const next = confirmedProfiles.filter(profile =>
                profile.processName.toLocaleLowerCase() !== processName.toLocaleLowerCase());
            next.push({ processName, inputChannels: channels.inputChannels,
                outputChannels: channels.outputChannels });
            const result = await window.vasio.replaceApplicationProfiles(next);
            setApplicationProfiles(result.profiles);
            setConfirmedProfiles(result.profiles);
            const clients = result.restartRequiredClients;
            setProfilesNotice(clients.length
                ? `Profiles saved. Restart ${clients.map(client => `${client.processName} (PID ${client.pid})`).join(', ')} to apply the changes.`
                : 'Profiles saved. New ASIO instances will use these values.');
        } catch (error) {
            setProfilesError(error.message);
            throw error;
        } finally {
            setProfilesSaving(false);
        }
    };
    const removeProcessConfiguration = async ({ processName, runtimePrefix, includeProfile }) => {
        if (!apiState || !configuration || configurationDirty || configurationLocked || profilesSaving || hardwareTransfer)
            throw new Error('Apply pending changes before removing a process configuration.');
        const original = configurationFromState(apiState);
        const profilePrefix = `virtual:timoxvasio:app:${processName.toLocaleLowerCase()}:`;
        const belongs = id => (runtimePrefix && id.startsWith(runtimePrefix)) ||
            id.toLocaleLowerCase().startsWith(profilePrefix);
        const remainingRoutes = original.routes.filter(route =>
            !belongs(route.sourceEndpointId) && !belongs(route.destinationEndpointId));
        const removedRoutes = original.routes.filter(route =>
            belongs(route.sourceEndpointId) || belongs(route.destinationEndpointId));
        const remainingProfiles = includeProfile ? confirmedProfiles.filter(profile =>
            profile.processName.toLocaleLowerCase() !== processName.toLocaleLowerCase()) : confirmedProfiles;
        const profileRemoved = remainingProfiles.length !== confirmedProfiles.length;
        if (!removedRoutes.length && !profileRemoved) return;
        setApplying(true);
        setNotice(`Removing ${removedRoutes.length} saved connections for ${processName}…`);
        let routesApplied = false;
        let profileApplied = false;
        try {
            if (removedRoutes.length) {
                await window.vasio.applyConfiguration({ ...original, routes: remainingRoutes });
                routesApplied = true;
            }
            if (profileRemoved) {
                const result = await window.vasio.replaceApplicationProfiles(remainingProfiles);
                profileApplied = true;
                setApplicationProfiles(result.profiles);
                setConfirmedProfiles(result.profiles);
            }
            await refreshState({ resetDraft: true });
            setRouteVisuals(current => {
                const next = { ...current };
                removedRoutes.forEach(route => { delete next[routeVisualKey(route)]; });
                return next;
            });
            setSelectedRouteId(null);
            setNotice(`${processName}: ${removedRoutes.length} saved ${removedRoutes.length === 1 ? 'connection removed' : 'connections removed'}${profileRemoved ? ' and profile removed' : ''}. Restart the application to release its current ASIO channels.`);
        } catch (error) {
            const recoveryErrors = [];
            if (profileApplied) {
                try {
                    const restoredProfiles = await window.vasio.replaceApplicationProfiles(confirmedProfiles);
                    setApplicationProfiles(restoredProfiles.profiles);
                    setConfirmedProfiles(restoredProfiles.profiles);
                } catch (rollbackError) { recoveryErrors.push(`profile: ${rollbackError.message}`); }
            }
            if (routesApplied) {
                try { await window.vasio.applyConfiguration(original); await refreshState({ resetDraft: true }); }
                catch (rollbackError) { recoveryErrors.push(`connections: ${rollbackError.message}`); }
            }
            const message = recoveryErrors.length ? `${error.message} Recovery failed for ${recoveryErrors.join('; ')}.` : error.message;
            setNotice(message);
            throw new Error(message);
        } finally { setApplying(false); }
    };
    const restoreHardwareTransfer = async (plan, state) => {
        const driver = state.physicalDrivers?.find(item => item.id === plan.targetDriverId);
        if (!driver?.capabilitiesKnown) {
            setNotice('The new physical driver did not publish its ports. The original connections remain saved for recovery.');
            return;
        }
        const result = transferRoutesToDriver(plan.routes, driver);
        if (result.missing.length) {
            setNotice(`The new driver lacks ${result.missing.join(', ')}. The original connections remain saved for recovery; choose a driver with these ports.`);
            return;
        }
        const confirmed = configurationFromState(state);
        const affected = new Set(plan.routes.map(route => route.id));
        const migrated = { ...confirmed, routes: [
            ...confirmed.routes.filter(route => !affected.has(route.id)), ...result.routes] };
        await window.vasio.applyConfiguration(migrated);
        const final = await refreshState({ resetDraft: true });
        const saved = configurationFromState(final).routes;
        if (!result.routes.every(route => saved.some(item => item.sourceEndpointId === route.sourceEndpointId &&
            item.destinationEndpointId === route.destinationEndpointId && item.gainDb === route.gainDb && item.mute === route.mute)))
            throw new Error('The new driver opened, but the migrated connections were not confirmed. Their backup remains available.');
        setHardwareTransfer(null);
        saveHardwareTransfer(null);
        setNotice(`${result.routes.length} hardware ${result.routes.length === 1 ? 'connection' : 'connections'} transferred to ${driver.name}.`);
    };
    const migrateHardwareConnections = async () => {
        if (!configuration?.physicalDriverId || configurationLocked || !incompatiblePhysicalRoutes.length) return;
        const plan = { targetDriverId: configuration.physicalDriverId,
            previousConfiguration: apiState ? configurationFromState(apiState) : null,
            routes: incompatiblePhysicalRoutes.map(route => ({ ...route })) };
        setHardwareTransfer(plan);
        saveHardwareTransfer(plan);
        setApplying(true);
        setNotice('Opening the selected physical driver; the existing connections are saved for transfer…');
        try {
            // Keep old-card endpoints persisted while discovering the target.
            // They remain dormant, and an absent card can still be restored.
            await window.vasio.applyConfiguration(configuration);
            const opened = await refreshState({ resetDraft: true });
            if (opened.engine.physicalDriverId !== plan.targetDriverId)
                throw new Error('The selected driver did not open. The original connections remain saved for recovery.');
            await restoreHardwareTransfer(plan, opened);
        } catch (error) { setNotice(error.message); }
        finally { setApplying(false); }
    };
    const retryHardwareTransfer = async () => {
        if (!hardwareTransfer || configurationLocked || !apiState) return;
        setApplying(true);
        try { await restoreHardwareTransfer(hardwareTransfer, apiState); }
        catch (error) { setNotice(error.message); }
        finally { setApplying(false); }
    };
    const restorePreviousPhysicalDriver = async () => {
        const previous = hardwareTransfer?.previousConfiguration;
        if (!previous || configurationLocked || !apiState) return;
        const previousSoftwareClock = previous.clockSource?.kind === 'software';
        setApplying(true);
        setNotice('Restoring the previous audio configuration and its connections…');
        try {
            if (!previousSoftwareClock && apiState.engine.physicalDriverId !== previous.physicalDriverId) {
                const current = configurationFromState(apiState);
                await window.vasio.applyConfiguration({ ...current,
                    ...(current.clockSource || previous.clockSource ? { clockSource: previous.clockSource || { kind: 'asio' } } : {}),
                    physicalDriverId: previous.physicalDriverId,
                    sampleRate: previous.sampleRate, bufferFrames: previous.bufferFrames,
                    routes: current.routes.filter(route => ![route.sourceEndpointId, route.destinationEndpointId]
                        .some(id => id.startsWith('physical:'))) });
                await refreshState({ resetDraft: true });
            }
            await window.vasio.applyConfiguration(previous);
            const restored = await refreshState({ resetDraft: true });
            const saved = configurationFromState(restored).routes;
            if (restored.engine.physicalDriverId !== previous.physicalDriverId ||
                (restored.engine.clockSource?.kind || 'asio') !== (previous.clockSource?.kind || 'asio') ||
                (previousSoftwareClock && (restored.engine.sampleRate !== previous.sampleRate ||
                    restored.engine.bufferFrames !== previous.bufferFrames)) ||
                !previous.routes.every(route => saved.some(item => item.sourceEndpointId === route.sourceEndpointId &&
                    item.destinationEndpointId === route.destinationEndpointId && item.gainDb === route.gainDb && item.mute === route.mute)))
                throw new Error('The previous driver or its connections could not be confirmed. The backup remains available.');
            setHardwareTransfer(null);
            saveHardwareTransfer(null);
            setNotice(previousSoftwareClock ? 'Previous software clock and connections restored.' : 'Previous driver and connections restored.');
        } catch (error) { setNotice(error.message); }
        finally { setApplying(false); }
    };
    const applyConfiguration = async () => {
        if (!configuration || configurationLocked || hardwareTransfer || invalidSoftwareFormat) return;
        if (incompatiblePhysicalRoutes.length) {
            setNotice('Use “Switch driver and transfer connections” to keep the saved connections when changing the physical driver.');
            return;
        }
        setApplying(true);
        setNotice('Applying configuration…');
        try {
            const latestState = await window.vasio.getState();
            const latestConfiguration = configurationFromState(latestState);
            const merged = mergeConfigurationDraft(
                draftBaseConfiguration.current || latestConfiguration,
                configuration, latestConfiguration);
            await window.vasio.applyConfiguration(Number.isInteger(latestState.configurationRevision)
                ? { ...merged, expectedConfigurationRevision: latestState.configurationRevision }
                : merged);
            const confirmed = await refreshState({ resetDraft: true });
            if (pendingMigrations.current.length) {
                const time = new Date().toISOString();
                const migrations = pendingMigrations.current;
                setMigrationLog(current => [...migrations.map(item => ({ time,
                    message: `Migrated connection: ${item.from.sourceEndpointId} → ${item.from.destinationEndpointId}` +
                        ` became ${item.to.sourceEndpointId} → ${item.to.destinationEndpointId}` })), ...current].slice(0, 100));
                pendingMigrations.current = [];
            }
            setNotice(confirmed.engine.state === 'running'
                ? 'Configuration applied. Audio engine running.'
                : confirmed.engine.state === 'stopped' && merged.routes.length === 0
                    ? 'Configuration applied. Audio engine stopped.'
                : confirmed.engine.state === 'stopped' && !confirmed.routes.length
                    ? 'Configured routes are waiting for their application channels to open.'
                : `Configuration applied. Audio engine ${confirmed.engine.state}.`);
        } catch (error) {
            setNotice(error.message);
        } finally {
            setApplying(false);
        }
    };

    const physicalDrivers = apiState?.physicalDrivers || [];
    const selectPhysicalDriver = driverId => updateDraft(current => ({
        ...current, ...(current.clockSource ? { clockSource: { kind: 'asio' } } : {}),
        physicalDriverId: driverId || null, sampleRate: null, bufferFrames: null
    }));
    const selectClockSource = kind => updateDraft(current => ({ ...current, clockSource: { kind },
        physicalDriverId: kind === 'software' ? null : engine?.physicalDriverId || null,
        sampleRate: kind === 'software' ? 48000 : null,
        bufferFrames: kind === 'software' ? 256 : null }));
    const dormantPhysicalRoutes = softwareClock ? (configuration?.routes || []).filter(route =>
        [route.sourceEndpointId, route.destinationEndpointId].some(endpointId => endpointId.startsWith('physical:'))) : [];
    const incompatiblePhysicalRoutes = softwareClock ? [] : (configuration?.routes || []).filter(route =>
        [route.sourceEndpointId, route.destinationEndpointId].some(endpointId =>
            endpointId.startsWith('physical:') &&
            (!configuration.physicalDriverId ||
                !endpointId.startsWith(`physical:${configuration.physicalDriverId}:`))));
    const keepRunningPhysicalDriver = () => {
        if (!apiState) return;
        const confirmed = configurationFromState(apiState);
        updateDraft(current => {
            const restored = { ...current, physicalDriverId: confirmed.physicalDriverId,
                sampleRate: confirmed.sampleRate, bufferFrames: confirmed.bufferFrames };
            if (confirmed.clockSource) restored.clockSource = confirmed.clockSource;
            else delete restored.clockSource;
            return restored;
        });
        if (hardwareTransfer && hardwareTransfer.targetDriverId !== confirmed.physicalDriverId &&
            incompatiblePhysicalRoutes.length > 0) {
            setHardwareTransfer(null);
            saveHardwareTransfer(null);
        }
        setNotice('');
    };
    const selectedPhysical = physicalDrivers.find(driver => driver.id === configuration?.physicalDriverId);
    const runningPhysical = physicalDrivers.find(driver => driver.id === engine?.physicalDriverId);
    const transferTarget = physicalDrivers.find(driver => driver.id === hardwareTransfer?.targetDriverId);
    const transferMissingPorts = hardwareTransfer && transferTarget?.capabilitiesKnown
        ? transferRoutesToDriver(hardwareTransfer.routes, transferTarget).missing : [];
    const clockSourceChangePending = softwareClock !== runningSoftwareClock;
    const physicalDriverChangePending = configuration?.physicalDriverId !== engine?.physicalDriverId || clockSourceChangePending;
    const availableRates = selectedPhysical?.sampleRates || [];
    const bufferSizes = (() => {
        const limits = selectedPhysical?.bufferSizes;
        if (!limits) return [];
        if (limits.granularity === -1) {
            return [64, 128, 256, 512, 1024, 2048, 4096, 8192].filter(size =>
                size >= limits.minimumFrames && size <= limits.maximumFrames);
        }
        if (limits.granularity > 0) {
            const sizes = [];
            for (let size = limits.minimumFrames; size <= limits.maximumFrames && sizes.length < 256; size += limits.granularity)
                sizes.push(size);
            return sizes;
        }
        return limits.preferredFrames > 0 ? [limits.preferredFrames] : [];
    })();
    const virtualDriver = apiState?.virtualDrivers.find(driver => driver.id === 'TimoxVasio');
    const bufferHealth = apiState?.bufferHealth;
    const measurements = apiState?.performanceMeasurements;
    const measurementWindow = measurements?.enabled ? measurements.window : null;
    const hasApplicationPath = (measurementWindow?.clients.length || 0) > 0;
    const routedPids = new Set((apiState?.routes || []).flatMap(route =>
        [route.sourceEndpointId, route.destinationEndpointId].map(id =>
            /^virtual:TimoxVasio:(\d+):/.exec(id)?.[1]).filter(Boolean).map(Number)));
    const routedClients = (virtualDriver?.clients || []).filter(client => routedPids.has(client.pid));
    const hasStageSamples = hasApplicationPath && [
        ...measurementWindow.clients.flatMap(client => [client.hostCallback, client.ringWrite, client.ringWait]),
        measurementWindow.physical.clientRead, measurementWindow.physical.routing, measurementWindow.physical.outputCopy
    ].some(duration => duration !== null);
    const formatStageNs = ns => ns < 1000000 ? `${(ns / 1000).toFixed(1)} µs` : `${(ns / 1000000).toFixed(2)} ms`;
    const stageDuration = (label, duration) => <div className="stage-measurement" key={label}>
        <span>{label}</span>
        <strong>{duration ? `Mean ${formatStageNs(duration.meanNs)} · Worst ${formatStageNs(duration.worstNs)}` : '—'}</strong>
        <small>{duration ? `${duration.samples} samples · ${measurementWindow.durationMs / 1000} s window` : 'No samples'}</small>
    </div>;
    const setStageMeasurements = async enabled => {
        setMeasurementsBusy(true);
        setMeasurementsError('');
        try {
            await window.vasio.executeCommand({ id: `performance-measurements-${Date.now()}`,
                command: 'performance.measurements.set', payload: { enabled } });
            await refreshState();
        } catch (error) { setMeasurementsError(error.message); }
        finally { setMeasurementsBusy(false); }
    };
    const activeClockDriver = runningSoftwareClock ? 'Software clock' : physicalDrivers.find(driver => driver.id === engine?.physicalDriverId)?.name || 'Selected physical ASIO driver';
    const blockMs = bufferHealth ? (bufferHealth.blockPeriodNs / 1000000).toFixed(2) : null;
    const worstMs = bufferHealth ? (bufferHealth.worstProcessingNs / 1000000).toFixed(2) : null;
    const worstDisplay = bufferHealth?.worstProcessingNs < 1000000
        ? `${(bufferHealth.worstProcessingNs / 1000).toFixed(1)} µs` : `${worstMs} ms`;
    const marginMs = bufferHealth ? (Math.abs(bufferHealth.worstMarginNs) / 1000000).toFixed(2) : null;
    const stereoPairs = apiState?.stereoPairs || [];
    const selectedStereoPair = stereoPairs.find(pair => pair.id === selectedStereoPairId);
    const applicationChannelSummaries = (virtualDriver?.clients || []).map(client => {
        const prefix = `virtual:TimoxVasio:${client.pid}:`;
        const routes = configuration?.routes || [];
        const routedInputs = new Set(routes
            .filter(route => route.destinationEndpointId.startsWith(`${prefix}input:`))
            .map(route => route.destinationEndpointId));
        const routedOutputs = new Set(routes
            .filter(route => route.sourceEndpointId.startsWith(`${prefix}output:`))
            .map(route => route.sourceEndpointId));
        return {
            ...client,
            routedInputCount: routedInputs.size,
            routedOutputCount: routedOutputs.size,
            inputMeterEndpoints: virtualDriver.inputEndpoints.filter(endpoint => routedInputs.has(endpoint.id)),
            outputMeterEndpoints: virtualDriver.outputEndpoints.filter(endpoint => routedOutputs.has(endpoint.id))
        };
    });

    const routedTransportDirections = applicationChannelSummaries.flatMap(client => [
        ...(client.outputMeterEndpoints.length ? [{ label: `${client.processName} output`, endpointId: client.outputMeterEndpoints[0].id }] : []),
        ...(client.inputMeterEndpoints.length ? [{ label: `${client.processName} input`, endpointId: client.inputMeterEndpoints[0].id }] : [])
    ]);
    const startCorrelation = async () => {
        if (!selectedStereoPairId) return;
        try { await window.vasio.startCorrelation(selectedStereoPairId); setCorrelationHistory([]); }
        catch (error) { setNotice(error.message); }
    };
    const configurationDirty = !!(apiState && configuration &&
        JSON.stringify(configuration) !== JSON.stringify(configurationFromState(apiState)));
    const discardDraft = () => {
        if (!apiState || configurationLocked || !configurationDirty) return;
        const saved = configurationFromState(apiState);
        draftBaseConfiguration.current = saved;
        setConfiguration(saved);
        setSelectedRouteId(null);
        pendingMigrations.current = [];
        setNotice('Pending edits discarded. Saved connections remain unchanged.');
    };
    const physicalSettingsPending = !!(configuration && engine && (
        clockSourceChangePending ||
        configuration.physicalDriverId !== engine.physicalDriverId ||
        (configuration.sampleRate && configuration.sampleRate !== engine.sampleRate) ||
        (configuration.bufferFrames && configuration.bufferFrames !== engine.bufferFrames)));
    const stopCorrelation = async () => {
        try { await window.vasio.stopCorrelation(); }
        catch (error) { setNotice(error.message); }
    };
    const runConsoleRequest = async () => {
        setConsoleBusy(true);
        try {
            const request = JSON.parse(consoleRequest);
            const result = consoleMode === 'http'
                ? await window.vasio.httpRequest(request.method, request.path, request.body)
                : await window.vasio.executeCommand(request);
            setConsoleResult(JSON.stringify(result, null, 2));
        } catch (error) {
            setConsoleResult(JSON.stringify({ code: error.code || 'CLIENT_ERROR', message: error.message }, null, 2));
        } finally { setConsoleBusy(false); setConsoleConfirmation(''); }
    };
    const executeConsoleRequest = () => {
        let request;
        try { request = JSON.parse(consoleRequest); }
        catch (error) { setConsoleResult(JSON.stringify({ code: 'INVALID_JSON', message: error.message }, null, 2)); return; }
        if (consoleMode === 'websocket' && ['engine.stop', 'configuration.apply'].includes(request.command)) {
            setConsoleConfirmation(request.command);
            return;
        }
        runConsoleRequest();
    };

    return (
        <div className={`App view-${activeView}`}>
            <header className="App-header">
                <div className="brand"><h1><a href="https://github.com/timox/TimoxVasio" target="_blank" rel="noopener noreferrer" title="Open the TimoxVasio GitHub repository">Timox VASIO Control</a></h1></div>
                <div className="status"><span className={`status-light ${engine?.state || 'disconnected'}`} />
                    {connectionError || `Engine: ${engine?.state || 'pending'}`}</div>
            </header>
            <nav className="view-tabs" aria-label="Application sections">
                <div className="view-tabs-heading">Sections</div>
                {[['configuration', 'Audio setup'], ['channels', 'Routing'], ['analysis', 'Analysis'], ['performance', 'Performance'], ['api', 'API and Swagger'], ['diagnostics', 'Logs']].map(([id, label]) =>
                    <button key={id} type="button" className={activeView === id ? 'selected' : ''} aria-current={activeView === id ? 'page' : undefined}
                        onClick={() => { setRoutingDialog(null); setActiveView(id); if (id === 'diagnostics') refreshDiagnostics(); }}>{label}</button>)}
                {activeView === 'channels' && <div className="view-tabs-actions">
                    <span className={configurationDirty ? 'draft-indicator pending' : 'draft-indicator'}>
                        {configurationDirty ? 'Changes pending' : 'Configuration applied'}</span>
                    <button type="button" className="apply-configuration" onClick={applyConfiguration}
                        disabled={!apiState || !configuration || !configurationDirty || configurationLocked || !!hardwareTransfer || invalidSoftwareFormat || incompatiblePhysicalRoutes.length > 0}>Apply configuration</button>
                    {configurationDirty && <button type="button" className="discard-draft" onClick={discardDraft}
                        disabled={configurationLocked || !!hardwareTransfer}>Discard pending edits</button>}
                </div>}
            </nav>
            {activeView === 'analysis' ? <main className="main-content api-console">
                <AnalysisTimeline performanceHistory={performanceHistory} transportHistory={transportHistory}
                    virtualClients={virtualDriver?.clients || []} routes={apiState?.routes || []}
                    configuredRoutes={confirmedRoutes}
                    engine={engine} onClear={() => { setPerformanceHistory([]); setTransportHistory([]); }} />
                <section className="section analysis-view"><h2>L/R stereo correlation</h2>
                    <p className="hint">Measures correlation between the two channels of an active output pair. Values range from −1 (out of phase) to +1 (in phase).</p>
                    <label className="field">L/R output pair
                        <select value={selectedStereoPairId} onChange={event => setSelectedStereoPairId(event.target.value)}>
                            <option value="">Select a routed pair</option>
                            {stereoPairs.map(pair => <option key={pair.id} value={pair.id}>{pair.label}</option>)}
                        </select>
                    </label>
                    <div className="correlation-readout" aria-live="polite"><strong>{correlation.correlation == null ? '—' : correlation.correlation.toFixed(2)}</strong>
                        <span>{correlation.state === 'measuring' ? 'Measuring' : correlation.state === 'no_signal' ? 'No measurable signal' : 'Analysis stopped'}</span></div>
                    <svg className="correlation-graph" viewBox="0 0 600 160" role="img" aria-label="Correlation history from minus one to plus one">
                        <line x1="0" x2="600" y1="80" y2="80" className="correlation-zero" />
                        <text x="5" y="16">+1 in phase</text><text x="5" y="85">0</text><text x="5" y="155">−1 out of phase</text>
                        <polyline points={correlationHistory.map((value, index) => `${75 + index * (520 / 39)},${80 - value * 58}`).join(' ')} />
                    </svg>
                    <div className="analysis-actions"><button type="button" onClick={startCorrelation} disabled={!selectedStereoPair || engine?.state !== 'running'}>Start analysis</button>
                        <button type="button" onClick={stopCorrelation} disabled={correlation.state === 'stopped'}>Stop analysis</button></div>
                </section>
            </main> : activeView === 'api' ? <main className="main-content api-console">
                <section className="section"><h2>HTTP reference</h2>
                    <p className="hint">OpenAPI specification bundled with the application. Schemas are included; no external service is loaded.</p>
                    <div className="swagger-frame"><iframe title="TimoxVasio Swagger documentation" src="swagger.html" /></div>
                </section>
                <section className="section"><h2>WebSocket commands and events</h2>
                    <p className="hint">Local connection using the subprotocol <code>vasio.api.v1</code>.</p>
                    <dl className="websocket-reference"><dt>Commands</dt><dd>{openApiDocument.paths['/api/v1/ws'].get['x-websocket'].commands.map(name => <code key={name}>{name}</code>)}</dd>
                        <dt>Configuration</dt><dd>`configuration.apply` sets the physical driver, sample rate, buffer size, and routes.</dd>
                        <dt>Stop</dt><dd>`engine.stop` is rejected while an ASIO client is connected.</dd>
                        <dt>Events</dt><dd>{openApiDocument.paths['/api/v1/ws'].get['x-websocket'].events.map(name => <code key={name}>{name}</code>)}</dd></dl>
                    <pre className="json-example">{JSON.stringify({ id: 'request-id', command: 'configuration.apply', payload: { physicalDriverId: null, sampleRate: null, bufferFrames: null, routes: [] } }, null, 2)}</pre>
                </section>
                <section className="section api-console-panel"><h2>Console API</h2>
                    <p className="hint">Send a documented WebSocket command or HTTP request to the local API.</p>
                    <label className="field">Request type<select value={consoleMode} onChange={event => setConsoleMode(event.target.value)}><option value="websocket">WebSocket command</option><option value="http">HTTP request</option></select></label>
                    {consoleMode === 'websocket' && <label className="field">Preset command<select value={consoleCommand} onChange={event => {
                        const command = event.target.value; setConsoleCommand(command);
                        const payload = command === 'configuration.apply' ? { physicalDriverId: null, sampleRate: null, bufferFrames: null, routes: [] } :
                            command === 'audio.correlation.start' ? { stereoPairId: selectedStereoPairId } : undefined;
                        const id = window.crypto?.randomUUID?.() || `api-${Date.now()}`;
                        setConsoleRequest(JSON.stringify({ id, command, ...(payload ? { payload } : {}) }, null, 2));
                    }}>
                        {['engine.start', 'engine.stop', 'configuration.apply', 'audio.correlation.start', 'audio.correlation.stop'].map(command => <option key={command}>{command}</option>)}
                    </select></label>}
                    {consoleMode === 'http' && <label className="field">Preset HTTP request<select onChange={event => setConsoleRequest(event.target.value)} defaultValue="">
                        <option value="" disabled>Select a request</option>
                        {[
                            ['GET', '/api/v1/state'], ['GET', '/api/v1/drivers'], ['GET', '/api/v1/openapi.json'],
                            ['GET', '/api/v1/schemas/api-v1.json'], ['GET', '/api/v1/diagnostics?limit=100'],
                            ['PUT', '/api/v1/diagnostics', { level: 'debug' }], ['GET', '/api/v1/application-profiles'],
                            ['PUT', '/api/v1/application-profiles', { profiles: [] }]
                        ].map(([method, path, body]) => <option key={`${method}-${path}`} value={JSON.stringify({ method, path, ...(body ? { body } : {}) })}>{method} {path}</option>)}
                    </select></label>}
                    <label className="field">Message JSON<textarea value={consoleRequest} onChange={event => setConsoleRequest(event.target.value)} rows={8} spellCheck="false" placeholder={'{"id":"...","command":"engine.start"}'} /></label>
                    <div className="analysis-actions"><button type="button" onClick={executeConsoleRequest} disabled={consoleBusy}>{consoleBusy ? 'Sending…' : 'Run'}</button></div>
                    <label className="field">Response or error<textarea readOnly value={consoleResult} rows={7} /></label>
                    {apiEvents.length > 0 && <details><summary>Recent events ({apiEvents.length})</summary><div className="api-event-stream">{apiEvents.slice(-12).reverse().map((event, index) => <pre key={`${event.time}-${index}`}>{event.event} · {JSON.stringify(event.payload)}</pre>)}</div></details>}
                    <dialog ref={confirmationDialog} className="confirmation-panel" role="alertdialog" aria-labelledby="api-confirmation-title"
                        onCancel={event => { event.preventDefault(); setConsoleConfirmation(''); }}>
                        <h3 id="api-confirmation-title">Confirm command</h3>
                        <p>{consoleConfirmation === 'engine.stop' ? 'Stop the audio engine?' : 'Apply this audio and routing configuration?'}</p>
                        <div className="analysis-actions"><button type="button" onClick={runConsoleRequest}>Confirm</button><button type="button" onClick={() => setConsoleConfirmation('')}>Cancel</button></div>
                    </dialog>
                </section>
            </main> : activeView === 'performance' ? <main className="main-content api-console">
                <BufferTimingDiagram sampleRate={engine?.sampleRate} bufferFrames={engine?.bufferFrames} />
                <section className="section buffer-health" aria-label={runningSoftwareClock ? 'Software callback performance' : 'Physical callback performance'}>
                    <h2>{runningSoftwareClock ? 'Software clock callback' : 'Physical ASIO callback'}</h2>
                    {!bufferHealth ? <p>No callback observation yet. Open an ASIO application and wait for a five-second window.</p> : <>
                        <p role="status">{bufferHealth.state === 'warning' ? 'Callback timing incidents were observed.' :
                            bufferHealth.state === 'insufficient' ? 'Insufficient callback data for this window.' :
                                'No repeated timing incidents in the observed window.'}</p>
                        <div className="performance-metrics">
                            <div className="performance-metric primary"><span>Slowest callback</span><strong>{worstDisplay}</strong></div>
                            <div className="performance-metric"><span>Time available</span><strong>{blockMs} ms</strong></div>
                            <div className="performance-metric"><span>{bufferHealth.worstMarginNs >= 0 ? 'Time left' : 'Over time'}</span><strong>{marginMs} ms</strong></div>
                            <div className="performance-metric"><span>Delayed starts</span><strong>{bufferHealth.delayedStartCount}</strong></div>
                            <div className="performance-metric"><span>Processing overruns</span><strong>{bufferHealth.processingOverrunCount}</strong></div>
                            <div className="performance-metric"><span>Callbacks observed</span><strong>{bufferHealth.callbackCount}</strong></div>
                        </div>
                        <div className="buffer-timeline" role="img" aria-label={`${worstMs} ms of processing within a ${blockMs} ms block; ${bufferHealth.worstMarginNs >= 0 ? `${marginMs} ms remaining` : `${marginMs} ms over the block time`}`}>
                            <span className="buffer-timeline-work" style={{ width: `${Math.min(100, Math.max(0, bufferHealth.worstProcessingNs / bufferHealth.blockPeriodNs * 100))}%` }} />
                            <span className="buffer-timeline-remaining" />
                        </div>
                        <p className="buffer-explanation">
                            {bufferHealth.callbackCount} callbacks were observed over {(bufferHealth.windowDurationMs / 1000).toFixed(1)} seconds.{' '}
                            {bufferHealth.delayedStartCount === 0 && bufferHealth.processingOverrunCount === 0
                                ? `None started unusually late or took longer than the ${blockMs} ms available per block. `
                                : `${bufferHealth.delayedStartCount} starts were unusually late; ${bufferHealth.processingOverrunCount} callbacks took longer than the ${blockMs} ms available per block. `}
                            {`The slowest callback took ${worstMs} ms, `}
                            {bufferHealth.worstMarginNs >= 0 ? `leaving ${marginMs} ms.`
                                : `exceeding the ${blockMs} ms available per block by ${marginMs} ms.`}
                        </p>
                        <p className="hint">{bufferHealth.sampleRate} Hz · {bufferHealth.bufferFrames} frames · {(bufferHealth.windowDurationMs / 1000).toFixed(1)} s window. Timing incidents do not by themselves prove an audible dropout.</p>
                    </>}
                </section>
                <section className="section routed-app-cpu" aria-label="Routed application CPU">
                    <h2>Routed application load</h2>
                    <p className="hint">Process CPU is its share of all logical processors, including work outside audio. The callback figure shows how much of one block its audio processing used. A busy host can be identified here; TimoxVasio cannot identify an individual VST inside it.</p>
                    {routedClients.length === 0 ? <p>No routed ASIO application is active.</p> :
                        <div className="routed-app-list">{routedClients.map(client => {
                            const hostCallback = measurementWindow?.clients.find(item => item.processId === client.pid)?.hostCallback;
                            const blockPeriodNs = bufferHealth?.blockPeriodNs ||
                                (engine?.sampleRate && engine?.bufferFrames
                                    ? engine.bufferFrames * 1000000000 / engine.sampleRate : null);
                            const callbackShare = hostCallback && blockPeriodNs
                                ? hostCallback.worstNs * 100 / blockPeriodNs : null;
                            return <article className="routed-app-card" key={client.pid}>
                                <div className="routed-app-heading"><h3>{client.processName}</h3><span>PID {client.pid}</span></div>
                                <div className="routed-app-readings">
                                    <div><span>Windows process CPU</span><strong>{typeof client.processCpuPercent === 'number'
                                        ? `${client.processCpuPercent.toFixed(1)}%` : '—'}</strong>
                                        <small>{client.processCpuWindowMs ? `Average over ${(client.processCpuWindowMs / 1000).toFixed(1)} s` : 'Waiting for another reading, or process access unavailable'}</small></div>
                                    <div className={callbackShare >= 100 ? 'callback-over-budget' : ''}>
                                        <span>Slowest host callback / block</span><strong>{callbackShare === null ? '—' : `${callbackShare.toFixed(1)}%`}</strong>
                                        <small>{callbackShare === null ? 'Enable Measure stages and wait for a 5 s window' :
                                            callbackShare >= 100 ? 'Audio deadline exceeded in a sampled callback' :
                                                `${formatStageNs(hostCallback.worstNs)} of ${formatStageNs(blockPeriodNs)} available`}</small></div>
                                </div>
                            </article>;
                        })}</div>}
                </section>
                <section className="section transport-health" aria-label="Virtual transport losses">
                    <h2>Virtual transport</h2>
                    <p>Underruns and overruns are cumulative for each routed client transport. They indicate incomplete or unconsumed blocks, not elapsed transport time.</p>
                    {routedTransportDirections.length === 0 ? <p>No routed virtual channels.</p> :
                        <div className="transport-table-wrap"><table className="transport-table"><thead><tr><th>Client transport</th><th>Underruns</th><th>Overruns</th></tr></thead>
                            <tbody>{routedTransportDirections.map(direction => <tr key={direction.endpointId}><th scope="row">{direction.label}</th>
                                <td>{meters[direction.endpointId]?.underruns ?? '—'}</td><td>{meters[direction.endpointId]?.overruns ?? '—'}</td></tr>)}</tbody>
                        </table></div>}
                    <p className="hint">Only directions used by active routes are shown. A dash means no meter event has arrived yet.</p>
                </section>
                <section className="section clock-source" aria-label="Audio clock source">
                    <span className="performance-kicker">Audio clock</span>
                    <h2>{runningSoftwareClock ? 'Software clock sets the pace' : 'One card sets the pace'}</h2>
                    <div className="clock-flow">
                        <div><strong>{activeClockDriver}</strong><span>{runningSoftwareClock ? 'Software clock callbacks' : 'Physical ASIO callbacks'}</span></div>
                        <span aria-hidden="true">→</span>
                        <div><strong>TimoxVasio</strong><span>Publishes virtual blocks</span></div>
                        <span aria-hidden="true">→</span>
                        <div><strong>ASIO applications</strong><span>Wake on those blocks</span></div>
                    </div>
                    {runningSoftwareClock ? <p>TimoxVasio schedules virtual blocks at the configured sample rate and quantum without a physical audio card. Timer jitter can still occur; callback measurements show the observed timing.</p> :
                        <p>TimoxVasio follows the selected card's audio clock instead of running an independent timer. This avoids accumulating clock drift between its virtual stream and that card. Callback jitter can still occur, and a separate audio device can still drift.</p>}
                </section>
                <section className="section performance-flow" aria-label="Audio latency path">
                    <h2>{runningSoftwareClock ? 'Between ASIO applications' : 'From the ASIO application to the audio output'}</h2>
                    <div className="stage-controls">
                        <label className="stage-toggle"><input type="checkbox" checked={!!measurements?.enabled}
                            disabled={measurementsBusy || !measurements}
                            onChange={event => setStageMeasurements(event.target.checked)} />
                            Measure stages
                        </label>
                        <span role="status">{!measurements?.enabled ? 'Measurements off' :
                            !measurementWindow ? 'Collecting first 5 s window' :
                            !hasApplicationPath ? 'No active ASIO application path' :
                            !hasStageSamples ? 'No stage samples' : 'Last complete 5 s window'}</span>
                    </div>
                    {measurementsError && <p className="error-panel" role="alert">{measurementsError}</p>}
                    <ol className="transport-steps">
                        <li><strong>Application produces a block</strong><span>Its ASIO callback writes the output samples into a shared ring in TimoxVasio.</span>
                            <div className="stage-measurements">{measurementWindow?.clients.length ? measurementWindow.clients.map(client =>
                                <div className="stage-client" key={client.processId}>
                                    <h3>{client.driverId} · PID {client.processId}</h3>
                                    {stageDuration('Host callback', client.hostCallback)}
                                    <small>The full work the application performs during its ASIO callback.</small>
                                    {stageDuration('Virtual ring write', client.ringWrite)}
                                </div>) : <span>—</span>}</div></li>
                        <li><strong>Engine reads the shared ring</strong><span>At the next {runningSoftwareClock ? 'software clock' : 'physical'} callback, the engine reads the completed block and applies the configured routes.</span>
                            <div className="stage-measurements">{measurementWindow?.clients.length ? measurementWindow.clients.map(client =>
                                <div className="stage-client" key={client.processId}>
                                    <h3>{client.driverId} · PID {client.processId}</h3>
                                    {stageDuration('Ring wait', client.ringWait)}
                                    <small>From publication to reading the same complete block; not acoustic latency.</small>
                                    <small>Discarded blocks: {client.ringWaitDiscardedBlocks}</small>
                                </div>) : <span>—</span>}</div></li>
                        <li><strong>{runningSoftwareClock ? 'Application inputs receive the routed block' : 'Physical ASIO driver receives the block'}</strong><span>{runningSoftwareClock ? 'The engine delivers the routed samples through virtual application channels.' : 'The routed output is handed to the selected card; hardware conversion adds its own delay.'}</span>
                            <div className="stage-measurements">
                                {stageDuration('Client read', hasApplicationPath ? measurementWindow?.physical.clientRead : null)}
                                {stageDuration('Routing', hasApplicationPath ? measurementWindow?.physical.routing : null)}
                                {!runningSoftwareClock && stageDuration('Physical output copy', hasApplicationPath ? measurementWindow?.physical.outputCopy : null)}
                            </div></li>
                    </ol>
                    <p>If the route passes through SuperCollider, the engine first delivers a block to that application. SuperCollider then publishes its output for a following {runningSoftwareClock ? 'software clock' : 'physical'} callback. A direct route skips this extra application stage.</p>
                    <p className="hint">At the selected rate and buffer size, one block represents {engine?.sampleRate && engine?.bufferFrames ? `${(engine.bufferFrames * 1000 / engine.sampleRate).toFixed(2)} ms` : 'an unknown duration'}. The complete application-to-speaker latency is not measured. The callback measurement above shows whether TimoxVasio finished routing each {runningSoftwareClock ? 'software clock' : 'physical'} block within the time available.</p>
                </section>
            </main> : activeView === 'diagnostics' ? <main className="main-content api-console">
                <section className="section diagnostics-view"><div className="section-heading"><div><h2>Engine logs</h2><p className="hint">Persistent file: %LOCALAPPDATA%\TimoxVasio\logs\engine.log</p></div>
                    <div className="diagnostics-actions"><label className="field">Log level<select value={diagnostics.level} onChange={event => setDiagnosticLevel(event.target.value)}><option value="info">Standard</option><option value="debug">Detailed</option></select></label>
                        <button type="button" onClick={refreshDiagnostics} disabled={diagnosticsLoading}>{diagnosticsLoading ? 'Refreshing…' : 'Refresh'}</button>
                        {(connectionError || engine?.state !== 'running') && <button type="button" className="engine-start" onClick={() => manageEngine('start')} disabled={!!engineAction}>{engineAction === 'start' ? 'Starting…' : 'Start audio engine'}</button>}
                        {!connectionError && (engine?.physicalDriverId || runningSoftwareClock) && <button type="button" className="engine-stop" onClick={() => manageEngine('stop')} disabled={!!engineAction || (virtualDriver?.clients.length || 0) > 0} title={(virtualDriver?.clients.length || 0) > 0 ? 'Close connected ASIO applications before stopping the engine.' : undefined}>{engineAction === 'stop' ? 'Stopping…' : 'Stop audio engine'}</button>}</div></div>
                    {diagnosticsError && <div className="error-panel">{diagnosticsError}</div>}
                    {migrationLog.length > 0 && <div className="migration-log" aria-label="Connection migrations">
                        <h3>Connection migration history</h3>
                        <ul>{migrationLog.map((entry, index) => <li key={`${entry.time}-${index}`}><time>{entry.time}</time> · {entry.message}</li>)}</ul>
                    </div>}
                    <div className="log-table-wrap"><table className="log-table"><thead><tr><th>UTC time</th><th>Level</th><th>Component</th><th>Message</th></tr></thead>
                        <tbody>{diagnostics.entries.map((entry, index) => <tr key={`${entry.timestamp}-${index}`}><td>{entry.timestamp}</td><td><span className={`log-level ${entry.level}`}>{entry.level}</span></td><td>{entry.component}</td><td>{entry.message}</td></tr>)}
                            {!diagnostics.entries.length && <tr><td colSpan="4" className="empty-log">No log entries recorded.</td></tr>}</tbody></table></div>
                </section>
            </main> : <main className="main-content api-console">
                {engine?.lastError && <div className="error-panel">{engine.lastError.message}</div>}
                {notice && <div className="notice" role="status">{notice}</div>}
                {['configuration', 'channels'].includes(activeView) && dormantPhysicalRoutes.length > 0 &&
                    <p className="hint" role="status">{dormantPhysicalRoutes.length} physical connections remain saved and inactive with the software clock. They resume when a compatible physical ASIO driver is selected.</p>}
                {['configuration', 'channels'].includes(activeView) && incompatiblePhysicalRoutes.length > 0 &&
                    <div className="driver-change-blocker" role="alert">
                        <strong>{physicalDriverChangePending ? 'Physical driver change not applied' : 'Connections target another physical driver'}</strong>
                        <p>Running: <b>{runningPhysical?.name || 'none'}</b> · Replacement: <b>{selectedPhysical?.name || 'choose a driver below'}</b>.</p>
                        <p>{incompatiblePhysicalRoutes.length} saved {incompatiblePhysicalRoutes.length === 1 ? 'connection still points' : 'connections still point'} to the old card. Select the replacement driver, then transfer the connections to the same channel numbers.</p>
                        <label className="field driver-change-target">Replacement physical ASIO driver
                            <select aria-label="Replacement physical ASIO driver" disabled={configurationLocked || !!hardwareTransfer}
                                value={selectedPhysical?.id || ''} onChange={event => selectPhysicalDriver(event.target.value)}>
                                <option value="">Choose a replacement driver…</option>
                                {physicalDrivers.filter(driver => driver.id !== engine?.physicalDriverId).map(driver =>
                                    <option key={driver.id} value={driver.id}>{driver.name}</option>)}
                            </select>
                        </label>
                        {physicalDrivers.every(driver => driver.id === engine?.physicalDriverId) &&
                            <p>No other physical ASIO driver is detected. Connect or install the replacement driver first.</p>}
                        <div className="driver-change-actions">
                            <button type="button" className="migrate-hardware-routes" disabled={configurationLocked || !!hardwareTransfer || !selectedPhysical || !physicalDriverChangePending}
                                onClick={migrateHardwareConnections}>{selectedPhysical ? `Switch driver and transfer ${incompatiblePhysicalRoutes.length} ${incompatiblePhysicalRoutes.length === 1 ? 'connection' : 'connections'}` : 'Choose a replacement driver first'}</button>
                            {physicalDriverChangePending && <button type="button" onClick={keepRunningPhysicalDriver}>Keep running driver</button>}
                        </div>
                    </div>}
                {['configuration', 'channels'].includes(activeView) && hardwareTransfer &&
                    <div className="driver-change-recovery" role="alert">
                        <strong>{hardwareTransfer.routes.length} {hardwareTransfer.routes.length === 1 ? 'connection saved' : 'connections saved'} for transfer</strong>
                        <p>The original connections are backed up in Control. The target driver is {transferTarget?.name || hardwareTransfer.targetDriverId}.</p>
                        {transferMissingPorts.length > 0 && <p>The new driver has no {transferMissingPorts.join(', ')}. Restore the previous driver, or choose another card with these channels.</p>}
                        {engine?.physicalDriverId === hardwareTransfer.targetDriverId ?
                            <button type="button" disabled={configurationLocked || !transferTarget?.capabilitiesKnown || transferMissingPorts.length > 0}
                                onClick={retryHardwareTransfer}>Restore connections on the new driver</button> :
                            <p>Open the target driver to discover its ports, then restore these connections.</p>}
                        {hardwareTransfer.previousConfiguration &&
                            <button type="button" disabled={configurationLocked} onClick={restorePreviousPhysicalDriver}>
                                {hardwareTransfer.previousConfiguration.clockSource?.kind === 'software'
                                    ? 'Restore previous software clock and connections' : 'Restore previous driver and connections'}
                            </button>}
                    </div>}

                {activeView === 'configuration' && <section className="section setup-physical">
                    <div className="setup-heading"><div><span className="setup-kind">▣ Audio clock</span><h2>{softwareClock ? 'Software clock' : 'Physical ASIO driver'}</h2></div>
                        <span className={`setup-state ${physicalSettingsPending ? 'pending' : 'applied'}`}>{physicalSettingsPending ? 'Changes pending' : engine?.physicalDriverId || runningSoftwareClock ? 'Configured' : 'No driver selected'}</span></div>
                    <label className="field">Clock source<select aria-label="Clock source" disabled={configurationLocked || !!hardwareTransfer}
                        value={softwareClock ? 'software' : 'asio'} onChange={event => selectClockSource(event.target.value)}>
                        <option value="asio">Physical ASIO driver</option><option value="software">Software clock (no audio card required)</option>
                    </select></label>
                    {clockSourceChangePending && !incompatiblePhysicalRoutes.length && <button type="button" onClick={keepRunningPhysicalDriver}>Keep running clock</button>}
                    {(engine?.physicalDriverId || runningSoftwareClock) && <div className="analysis-actions"><button type="button" className="engine-reset"
                        disabled={!!engineAction || applying || configurationLocked}
                        onClick={() => manageEngine('reset')}>{engineAction === 'reset' ? 'Restarting clock…' : runningSoftwareClock ? 'Reset software clock' : 'Reset physical driver'}</button>
                        <span className="hint">Briefly interrupts audio and reopens the last saved configuration; saved connections are kept.</span></div>}
                    {softwareClock ? <>
                        <label className="field">Software sample rate (Hz)<input aria-label="Software sample rate" type="number" min="8000" max="384000" step="1"
                            disabled={configurationLocked} value={configuration.sampleRate ?? ''} onChange={event => updateConfiguration('sampleRate', Number(event.target.value))} /></label>
                        <label className="field">Software quantum (frames)<input aria-label="Software quantum" type="number" min="16" max="8192" step="1"
                            disabled={configurationLocked} value={configuration.bufferFrames ?? ''} onChange={event => updateConfiguration('bufferFrames', Number(event.target.value))} /></label>
                        <p className="hint">Routes between ASIO applications run without a physical audio card. Default: 48000 Hz / 256 frames.</p>
                        {invalidSoftwareFormat && <p role="alert">Use an integer rate from 8000 to 384000 Hz and quantum from 16 to 8192 frames.</p>}
                    </> : incompatiblePhysicalRoutes.length > 0 ?
                        <p className="hint">Choose the replacement driver in the warning above, or keep the running driver.</p> :
                        <label className="field">Selected driver
                            <select disabled={configurationLocked} value={configuration?.physicalDriverId || ''}
                                onChange={event => selectPhysicalDriver(event.target.value)}>
                                <option value="">None (engine stopped)</option>
                                {physicalDrivers.map(driver => <option key={driver.id} value={driver.id}>{driver.name}</option>)}
                            </select>
                        </label>}
                    {configuration?.physicalDriverId && <>
                        <label className="field">Physical sample rate
                            <select disabled={configurationLocked} value={configuration.sampleRate || ''} onChange={event => updateConfiguration('sampleRate', event.target.value ? Number(event.target.value) : null)}>
                                <option value="">Current driver rate</option>
                                {availableRates.map(rate => <option key={rate} value={rate}>{rate} Hz</option>)}
                            </select>
                        </label>
                        <label className="field">Physical buffer size
                            <select disabled={configurationLocked} value={configuration.bufferFrames || ''} onChange={event => updateConfiguration('bufferFrames', event.target.value ? Number(event.target.value) : null)}>
                                <option value="">Driver preferred size</option>
                                {bufferSizes.map(frames => <option key={frames} value={frames}>{frames} frames</option>)}
                            </select>
                        </label>
                        <p className="hint">Physical ports are discovered after these settings are applied.</p>
                    </>}
                </section>}

                {activeView === 'configuration' && <section className="section setup-profiles">
                    <div className="setup-heading"><div><span className="setup-kind">◇ Capacity for applications</span><h2>Application channel profiles</h2></div>
                        <span className="setup-state applied">{confirmedProfiles.length} saved</span></div>
                    <p className="hint">Assign a channel profile to an existing process from its block in Routing. Use “Show all ports” there if it has no connections yet. The profile takes effect when the application opens ASIO again.</p>
                    {profilesError && <div className="error-panel" role="alert">{profilesError}</div>}
                    {profilesNotice && <div className="notice" role="status">{profilesNotice}</div>}
                    {profilesLoading ? <p className="hint">Loading profiles from the API…</p> : applicationProfiles && <>
                        <div className="profile-table-scroll">
                            <table className="application-profile-table">
                                <thead><tr><th>Executable</th><th>Input channels</th><th>Output channels</th></tr></thead>
                                <tbody>
                                    {applicationProfiles.map((profile, index) => <tr key={`${profile.processName}-${index}`}>
                                        <td>{profile.processName}</td>
                                        <td>{profile.inputChannels}</td>
                                        <td>{profile.outputChannels}</td>
                                    </tr>)}
                                    {!applicationProfiles.length && <tr><td colSpan="3">No application profiles configured.</td></tr>}
                                </tbody>
                            </table>
                        </div>
                        <button type="button" onClick={() => setActiveView('channels')}>Open Routing to configure a process</button>
                    </>}
                </section>}

                {activeView === 'channels' && <div className={`apply-bar${configurationDirty ? ' pending' : ''}`} role="status" aria-live="polite">
                    <span>{hardwareTransfer ? 'Connection transfer needs attention — use the recovery options above.' : incompatiblePhysicalRoutes.length > 0
                        ? 'Driver change blocked — resolve the old hardware connections above.' : configurationDirty
                        ? 'Pending changes — Apply configuration to activate the draft; audio routing will briefly pause.'
                        : 'Configuration applied — saved routes match the engine. Check active counts below.'}</span>
                    <button className="apply-configuration" onClick={applyConfiguration}
                        disabled={!apiState || !configuration || !configurationDirty || configurationLocked || !!hardwareTransfer || invalidSoftwareFormat || incompatiblePhysicalRoutes.length > 0}>Apply configuration</button>
                    {configurationDirty && <button type="button" className="discard-draft" onClick={discardDraft}
                        disabled={configurationLocked || !!hardwareTransfer}>Discard pending edits</button>}
                </div>}

                {activeView === 'configuration' && apiState?.apiVersion === '1.1' && <section className="section" aria-label="Windows audio bridges">
                    <h2>Windows audio bridges</h2>
                    <p className="hint">Capture a Windows input, play to a Windows output, or capture an output with endpoint loopback. Right-click the Routing background to add a bridge, or its block to configure it. Apply a new bridge first to discover its channels, then add connections.</p>
                    <div className="analysis-actions"><button type="button" onClick={() => setActiveView('channels')}>Open Routing for Windows audio</button>
                        <button type="button" disabled={bridgeInventoryBusy || !window.vasio.getWasapiDevices} onClick={refreshBridgeInventory}>Refresh Windows endpoints</button></div>
                    {apiState.wasapiInventoryError && <p role="alert">Windows endpoint discovery: {apiState.wasapiInventoryError}</p>}
                    {!(configuration?.wasapiBridges || []).length && <p>No Windows audio bridges configured.</p>}
                    {(configuration?.wasapiBridges || []).map(bridge => <article className="application-channel-card" key={bridge.id}>
                        <h3>{bridge.id} · {bridge.kind}</h3><BridgeStatus bridge={bridge} runtime={apiState.wasapiBridges?.find(item => item.id === bridge.id)} devices={apiState.wasapiDevices || []} />
                    </article>)}
                </section>}
                {activeView === 'channels' && <section className="section routing-workspace-section">
                    <h2>Routing</h2>
                    <p className="hint">Right-click the background, a process, or a cable for its actions. Details open in a dialog.</p>
                    <RoutingPatchbay sourceGroups={patchbayGroups.sources}
                        destinationGroups={patchbayGroups.destinations} routes={configuration?.routes || []}
                        confirmedRoutes={confirmedRoutes} activeRoutes={apiState?.routes || []}
                        meters={meters} onToggleRoute={toggleRoute} onReconnectRoute={reconnectRoute} selectedRouteId={selectedRouteId}
                        routeVisuals={routeVisuals} onChangeRouteVisual={updateRouteVisual}
                        onBulkChangeRouteVisuals={updateManyRouteVisuals}
                        onSelectRoute={setSelectedRouteId} onEditRoute={editRoute}
                        profiles={confirmedProfiles} onAssignProfile={assignProfileToProcess}
                        onRemoveProcessConfiguration={removeProcessConfiguration}
                        onTransferProcessConnections={moveProcessConnections}
                        onCreateConnection={processName => setRoutingDialog({ kind: 'add', processName })}
                        onShowProcessDetail={processName => setRoutingDialog({ kind: 'process', processName })}
                        onCreateBridge={apiState?.apiVersion === '1.1' ? () => setBridgeDialog({ kind: 'add' }) : undefined}
                        onShowNativeAudio={() => setNativeDialogOpen(true)}
                        onEditBridge={id => setBridgeDialog({ kind: 'edit', bridge: configuration.wasapiBridges.find(item => item.id === id) })}
                        onRemoveBridge={id => setBridgeDialog({ kind: 'remove', bridge: configuration.wasapiBridges.find(item => item.id === id) })}
                        isConnectionAllowed={bridgeConnectionAllowed}
                        disabled={configurationLocked} />
                </section>}
                {activeView === 'channels' && routingDialog && createPortal(<div className="routing-dialog-backdrop" onMouseDown={event => {
                    if (event.target === event.currentTarget) setRoutingDialog(null);
                }}><div className="routing-dialog" role="dialog" aria-modal="true" aria-label={routingDialog.kind === 'add' ? 'Add a connection' : 'Process and channel detail'}>
                    <div className="routing-dialog-heading"><h2>{routingDialog.kind === 'add' ? 'Add a connection' : 'Process and channel detail'}</h2>
                        <button type="button" onClick={() => setRoutingDialog(null)}>Close</button></div>
                    {routingDialog.kind === 'add' ? <QuickRouting sourceGroups={patchbayGroups.sources}
                        destinationGroups={patchbayGroups.destinations} routes={configuration?.routes || []}
                        onAddRoutes={addRoutes} resolveEndpointId={stableEndpointId}
                        isConnectionAllowed={bridgeConnectionAllowed}
                        preferredProcessName={routingDialog.processName || ''}
                        disabled={configurationLocked} /> : <>
                    <p className="hint">“Open” shows channels advertised by the application. “Routed” shows channels connected in the displayed configuration. Open channels are not routed automatically.</p>
                    {applicationChannelSummaries.filter(client => !routingDialog.processName || client.processName.toLocaleLowerCase() === routingDialog.processName.toLocaleLowerCase()).length === 0 ? <p className="hint">No application channels are currently open.</p> :
                        <div className="application-channel-list">
                            {applicationChannelSummaries.filter(client => !routingDialog.processName || client.processName.toLocaleLowerCase() === routingDialog.processName.toLocaleLowerCase()).map(client => <article className="application-channel-card" key={client.pid}>
                                <h3>{client.processName}</h3>
                                <div className="application-channel-directions">
                                    <div><strong>Inputs</strong><span>{client.inputChannels.length} open</span><span>{client.routedInputCount} routed</span></div>
                                    <div><strong>Outputs</strong><span>{client.outputChannels.length} open</span><span>{client.routedOutputCount} routed</span></div>
                                </div>
                                {(client.inputMeterEndpoints.length + client.outputMeterEndpoints.length) > 0 && <div className="active-channel-meters" role="region" tabIndex="0" aria-label={`Active levels for ${client.processName}`}>
                                    {[...client.inputMeterEndpoints, ...client.outputMeterEndpoints].map(endpoint => <ChannelMeter key={endpoint.id} endpoint={endpoint} meter={meters[endpoint.id]} />)}
                                </div>}
                            </article>)}
                        </div>}
                    </>}
                </div></div>, document.body)}



                {activeView === 'channels' && nativeDialogOpen && <NativeAudioDialog status={apiState?.nativeAudio}
                    connected={!connectionError} onClose={() => setNativeDialogOpen(false)} />}
                {bridgeDialog && <BridgeDialog key={`${bridgeDialog.kind}:${bridgeDialog.bridge?.id || ''}`} dialog={bridgeDialog}
                    bridges={configuration?.wasapiBridges || []} routes={configuration?.routes || []}
                    devices={apiState?.wasapiDevices || []} runtime={apiState?.wasapiBridges?.find(item => item.id === bridgeDialog.bridge?.id)}
                    inventoryError={apiState?.wasapiInventoryError} disabled={configurationLocked || !!hardwareTransfer}
                    onClose={() => setBridgeDialog(null)} onSave={bridge => {
                        updateDraft(current => ({ ...current, wasapiBridges: [...(current.wasapiBridges || []).filter(item => item.id !== bridge.id), bridge] }));
                        setBridgeDialog(null);
                    }} onRemove={id => {
                        updateDraft(current => ({ ...current, wasapiBridges: current.wasapiBridges.filter(item => item.id !== id),
                            routes: current.routes.filter(route => ![route.sourceEndpointId, route.destinationEndpointId].some(endpoint => endpoint.startsWith(`wasapi:${id}:`))) }));
                        setBridgeDialog(null);
                    }} />}
                {activeView === 'configuration' && <div className="analysis-actions"><button className="apply-configuration" onClick={applyConfiguration} disabled={!apiState || !configuration || configurationLocked || !!hardwareTransfer || invalidSoftwareFormat || incompatiblePhysicalRoutes.length > 0}>
                    Apply configuration
                </button>{configurationDirty && <button type="button" className="discard-draft" onClick={discardDraft}
                    disabled={configurationLocked || !!hardwareTransfer}>Discard pending edits</button>}</div>}
            </main>}
        </div>
    );
}

function BridgeStatus({ bridge, runtime, devices }) {
    const device = devices.find(item => item.id === (runtime?.effectiveInfo?.endpointId || bridge.endpointId));
    const format = runtime?.effectiveInfo?.format;
    return <>
        <p>Desired: {bridge.enabled === false ? 'disabled' : 'enabled'} · Stream: {runtime?.state || 'not opened'} · {device?.name || bridge.endpointId || `Default ${bridge.defaultRole}`}</p>
        {runtime?.effectiveInfo?.endpointId && <p className="hint">Resolved endpoint: {runtime.effectiveInfo.endpointId}</p>}
        {format && <p>{format.sampleRate} Hz · {format.channels} channels · {format.encoding} {format.validBits}/{format.containerBits} bits</p>}
        {device?.error && <p role="alert">{device.error}</p>}
        {runtime?.state === 'retrying' && <p>Retry {runtime.retryAttempt} in {runtime.retryInMs} ms</p>}
        {runtime?.health && <p className="hint">Buffered: {runtime.health.bufferedFrames ?? '—'} graph frames · Rejected: {runtime.health.rejectedFrames ?? '—'} graph frames · Silenced: {runtime.health.silencedFrames ?? '—'} graph frames
            {runtime.health.error ? ` · Stream error: ${runtime.health.error}` : ''}
            {runtime.health.mmcssError ? ` · Scheduling error: ${runtime.health.mmcssError}` : ''}
            {runtime.health.primingTimedOut ? ' · Priming timed out' : ''}
            {runtime.health.controlBacklogUpdates != null ? ` · Control backlog observations: ${runtime.health.controlBacklogUpdates}` : ''}</p>}
    </>;
}

function BridgeDialog({ dialog, bridges, routes, devices, runtime, inventoryError, disabled, onClose, onSave, onRemove }) {
    const [draft, setDraft] = useState(() => ({ id: '', kind: 'capture', enabled: true, bufferDuration100ns: 0, endpointId: '', ...dialog.bridge }));
    const [selection, setSelection] = useState(dialog.bridge?.defaultRole ? 'default' : 'endpoint');
    const dialogRef = useRef(null);
    const closeRef = useRef(onClose);
    closeRef.current = onClose;
    useEffect(() => {
        const previous = document.activeElement;
        dialogRef.current?.querySelector('input,button')?.focus();
        const keydown = event => {
            if (event.key === 'Escape') closeRef.current();
            if (event.key !== 'Tab') return;
            const controls = [...dialogRef.current.querySelectorAll('button:not(:disabled),input:not(:disabled),select:not(:disabled)')];
            const first = controls[0], last = controls[controls.length - 1];
            if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
            else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
        };
        document.addEventListener('keydown', keydown);
        return () => { document.removeEventListener('keydown', keydown); previous?.focus(); };
    }, []);
    const set = (key, value) => setDraft(current => ({ ...current, [key]: value }));
    const choices = devices.filter(item => item.flow === (draft.kind === 'capture' ? 'capture' : 'render'));
    const selected = choices.find(item => item.id === draft.endpointId);
    const usedRoutes = routes.filter(route => [route.sourceEndpointId, route.destinationEndpointId].some(id => id.startsWith(`wasapi:${draft.id}:`)));
    const valid = /^[A-Za-z0-9_-]{1,128}$/.test(draft.id) &&
        (dialog.kind !== 'add' || (bridges.length < 32 && !bridges.some(item => item.id === draft.id))) &&
        Number.isInteger(draft.bufferDuration100ns) && draft.bufferDuration100ns >= 0 && draft.bufferDuration100ns <= 10000000 &&
        (selection === 'default' || !!draft.endpointId);
    const save = () => {
        if (!valid || disabled) return;
        const { endpointId, defaultRole, ...common } = draft;
        onSave({ ...common, ...(selection === 'default' ? { defaultRole: defaultRole || 'console' } : { endpointId }) });
    };
    return createPortal(<div className="routing-dialog-backdrop" onMouseDown={event => { if (event.target === event.currentTarget) onClose(); }}>
        <div className="routing-dialog" role="dialog" aria-modal="true" aria-label={dialog.kind === 'remove' ? 'Remove Windows audio bridge' : 'Configure Windows audio bridge'} ref={dialogRef}>
            <div className="routing-dialog-heading"><h2>{dialog.kind === 'remove' ? 'Remove Windows audio bridge' : 'Windows audio bridge'}</h2><button type="button" onClick={onClose}>Close</button></div>
            {dialog.kind === 'remove' ? <><p>Remove {draft.id} and {usedRoutes.length} saved connection{usedRoutes.length === 1 ? '' : 's'}? This takes effect when you apply the configuration.</p>
                <button type="button" disabled={disabled} onClick={() => onRemove(draft.id)}>Remove bridge and its connections</button></> : <>
                <label className="field">Bridge ID<input aria-label="Bridge ID" maxLength="128" disabled={disabled || dialog.kind !== 'add'} value={draft.id} onChange={event => set('id', event.target.value)} /></label>
                <p className="hint">Use letters, digits, underscores or hyphens. The ID stays fixed so saved channel connections remain stable.</p>
                <label className="field">Mode<select aria-label="Bridge kind" disabled={disabled || (dialog.kind !== 'add' && usedRoutes.length > 0)} value={draft.kind}
                    onChange={event => setDraft(current => ({ ...current, kind: event.target.value, endpointId: '' }))}>
                    <option value="capture">Capture Windows input</option><option value="render">Play to Windows output</option><option value="endpointLoopback">Capture Windows output (endpoint loopback)</option>
                </select></label>
                {usedRoutes.length > 0 && <p className="hint">Mode is fixed while this bridge has saved connections. Disabling the bridge keeps those connections.</p>}
                <label className="field">Endpoint selection<select aria-label="Bridge selection" disabled={disabled} value={selection} onChange={event => setSelection(event.target.value)}>
                    <option value="endpoint">Specific endpoint</option><option value="default">Windows default role</option></select></label>
                {selection === 'default' ? <label className="field">Role<select aria-label="Default endpoint role" disabled={disabled} value={draft.defaultRole || 'console'} onChange={event => set('defaultRole', event.target.value)}>
                    <option value="console">Console</option><option value="multimedia">Multimedia</option><option value="communications">Communications</option></select></label> :
                    <label className="field">Windows endpoint<select aria-label="Windows endpoint" disabled={disabled} value={draft.endpointId || ''} onChange={event => set('endpointId', event.target.value)}>
                        <option value="">Select an endpoint</option>
                        {draft.endpointId && !selected && <option value={draft.endpointId}>{draft.endpointId} (unavailable)</option>}
                        {choices.map(item => <option key={item.id} value={item.id}>{item.name}{item.state === 1 ? '' : ' (unavailable)'}</option>)}
                    </select></label>}
                {selected?.error && <p role="alert">{selected.error}</p>}
                {inventoryError && <p role="alert">Windows endpoint discovery: {inventoryError}</p>}
                <label className="bridge-enabled-field"><input type="checkbox" aria-label="Bridge enabled" disabled={disabled} checked={draft.enabled} onChange={event => set('enabled', event.target.checked)} /> Enabled</label>
                <label className="field">Requested buffer duration (ms; 0 = system default)<input aria-label="Bridge buffer duration (ms)" type="number" min="0" max="1000" step="0.0001" disabled={disabled} value={draft.bufferDuration100ns / 10000} onChange={event => set('bufferDuration100ns', Math.round(Number(event.target.value) * 10000))} /></label>
                {runtime && <BridgeStatus bridge={dialog.bridge} runtime={runtime} devices={devices} />}
                <p className="hint">Windows audio uses its own clock. Bridges adapt rate and add buffering. Endpoint loopback to the same Windows output is not allowed.</p>
                {!valid && <p role="status">Choose an endpoint or default role, a unique valid ID, and a duration between 0 and 1000 ms.</p>}
                <button type="button" disabled={disabled || !valid} onClick={save}>Save bridge to draft</button>
            </>}
        </div></div>, document.body);
}

function ChannelMeter({ endpoint, meter }) {
    const peak = typeof meter?.peakDbfs === 'number' ? meter.peakDbfs : null;
    const visiblePeak = peak === null ? -60 : Math.max(-60, Math.min(0, peak));
    const width = peak === null ? 0 : ((visiblePeak + 60) / 60) * 100;
    return <div className="channel-meter" aria-label={`${endpoint.name}: ${peak === null ? 'measurement pending' : `${peak.toFixed(1)} dBFS`}`}>
        <span title={endpoint.name}>{endpoint.name}</span>
        <div className="meter-track" role="meter" aria-valuemin="-60" aria-valuemax="0"
            aria-valuenow={peak === null ? -60 : visiblePeak}
            aria-valuetext={peak === null ? 'Measurement pending' : `${peak.toFixed(1)} dBFS`}>
            <i style={{ width: `${width}%` }} />
        </div>
        <output>{peak === null ? 'Pending' : `${peak.toFixed(1)} dBFS`}</output>
    </div>;
}

export default App;
