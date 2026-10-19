import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import './App.css';
import './ProfessionalTheme.css';
import RoutingPatchbay from './RoutingPatchbay';
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
    const [configuration, setConfiguration] = useState(null);
    const [engine, setEngine] = useState(null);
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
            if (action === 'reset') setNotice('Physical driver reopened with the last saved connections. Pending edits were not applied.');
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
        const state = await window.vasio.getState();
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
                    if (!status.connected) setConnectionError(status.error || 'The VASIO engine is stopped');
                    else { setConnectionError(''); refreshState().catch(error => setConnectionError(error.message)); }
                });
                diagnosticListener = window.electronAPI.onEngineDiagnostic(message => {
                    setNotice(message);
                    setApiEvents(current => [...current, { time: new Date().toISOString(), event: 'engine.diagnostic', payload: { message } }].slice(-500));
                });
                const unsubscribeDisconnect = window.vasio.onDisconnect(() => {
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
            window.vasio.getState().then(state => {
                recordPerformanceState(state);
                setApiState(state);
                setEngine(state.engine);
                setConnectionError('');
            }).catch(error => setConnectionError(error.message));
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
        const physical = apiState.physicalDrivers;
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
    }, [apiState, confirmedProfiles]);
    const patchbayGroups = useMemo(() => {
        if (!apiState) return { sources: [], destinations: [] };
        const physical = apiState.physicalDrivers || [];
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
        return {
            sources: [
                ...virtualGroups('output'),
                ...profileGroups('output'),
                ...savedGroups('output'),
                ...physical.filter(driver => driver.inputEndpoints.length).map(driver => ({
                    id: `${driver.id}:input`, label: `${driver.name} · Inputs`, kind: 'physical-input',
                    endpoints: driver.inputEndpoints
                }))
            ],
            destinations: [
                ...physical.filter(driver => driver.outputEndpoints.length).map(driver => ({
                    id: `${driver.id}:output`, label: `${driver.name} · Outputs`, kind: 'physical-output',
                    endpoints: driver.outputEndpoints
                })),
                ...virtualGroups('input'),
                ...profileGroups('input'),
                ...savedGroups('input')
            ]
        };
    }, [apiState, endpoints.profiled, configuration?.routes]);
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
        sourceEndpointId = stableEndpointId(sourceEndpointId);
        destinationEndpointId = stableEndpointId(destinationEndpointId);
        if (configurationLocked || !current || current.routes.some(route => route.id !== routeId &&
            route.sourceEndpointId === sourceEndpointId && route.destinationEndpointId === destinationEndpointId)) return current;
        return { ...current, routes: current.routes.map(route => route.id === routeId
            ? { ...route, sourceEndpointId, destinationEndpointId } : route) };
    });
    const addRoutes = routes => updateDraft(current => ({ ...current,
        routes: [...current.routes, ...routes.map((route, index) => ({
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
        const migrated = { ...confirmed, routes: [...confirmed.routes, ...result.routes] };
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
            const affected = new Set(plan.routes.map(route => route.id));
            await window.vasio.applyConfiguration({ ...configuration,
                routes: configuration.routes.filter(route => !affected.has(route.id)) });
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
        if (!previous?.physicalDriverId || configurationLocked || !apiState) return;
        setApplying(true);
        setNotice('Restoring the previous physical driver and its connections…');
        try {
            if (apiState.engine.physicalDriverId !== previous.physicalDriverId) {
                const current = configurationFromState(apiState);
                await window.vasio.applyConfiguration({ ...current,
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
                !previous.routes.every(route => saved.some(item => item.sourceEndpointId === route.sourceEndpointId &&
                    item.destinationEndpointId === route.destinationEndpointId && item.gainDb === route.gainDb && item.mute === route.mute)))
                throw new Error('The previous driver or its connections could not be confirmed. The backup remains available.');
            setHardwareTransfer(null);
            saveHardwareTransfer(null);
            setNotice('Previous driver and connections restored.');
        } catch (error) { setNotice(error.message); }
        finally { setApplying(false); }
    };
    const applyConfiguration = async () => {
        if (!configuration || configurationLocked || hardwareTransfer) return;
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
            setNotice(merged.routes.length === 0
                ? 'Physical driver configured. The audio engine remains stopped until a route is defined.'
                : confirmed.engine.state === 'stopped' && !confirmed.routes.length
                    ? 'Configured routes are waiting for their application channels to open.'
                : 'Configuration applied. Audio routing was briefly interrupted.');
        } catch (error) {
            setNotice(error.message);
        } finally {
            setApplying(false);
        }
    };

    const physicalDrivers = apiState?.physicalDrivers || [];
    const selectPhysicalDriver = driverId => updateDraft(current => ({
        ...current, physicalDriverId: driverId || null, sampleRate: null, bufferFrames: null
    }));
    const incompatiblePhysicalRoutes = (configuration?.routes || []).filter(route =>
        [route.sourceEndpointId, route.destinationEndpointId].some(endpointId =>
            endpointId.startsWith('physical:') &&
            (!configuration.physicalDriverId ||
                !endpointId.startsWith(`physical:${configuration.physicalDriverId}:`))));
    const keepRunningPhysicalDriver = () => {
        if (!apiState) return;
        const confirmed = configurationFromState(apiState);
        updateDraft(current => ({ ...current, physicalDriverId: confirmed.physicalDriverId,
            sampleRate: confirmed.sampleRate, bufferFrames: confirmed.bufferFrames }));
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
    const physicalDriverChangePending = configuration?.physicalDriverId !== engine?.physicalDriverId;
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
    const activeClockDriver = physicalDrivers.find(driver => driver.id === engine?.physicalDriverId)?.name || 'Selected physical ASIO driver';
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
                {[['configuration', 'ASIO setup'], ['channels', 'Routing'], ['analysis', 'Analysis'], ['performance', 'Performance'], ['api', 'API and Swagger'], ['diagnostics', 'Logs']].map(([id, label]) =>
                    <button key={id} type="button" className={activeView === id ? 'selected' : ''} aria-current={activeView === id ? 'page' : undefined}
                        onClick={() => { setRoutingDialog(null); setActiveView(id); if (id === 'diagnostics') refreshDiagnostics(); }}>{label}</button>)}
                {activeView === 'channels' && <div className="view-tabs-actions">
                    <span className={configurationDirty ? 'draft-indicator pending' : 'draft-indicator'}>
                        {configurationDirty ? 'Changes pending' : 'Configuration applied'}</span>
                    <button type="button" className="apply-configuration" onClick={applyConfiguration}
                        disabled={!apiState || !configuration || !configurationDirty || configurationLocked || !!hardwareTransfer || incompatiblePhysicalRoutes.length > 0}>Apply configuration</button>
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
                <section className="section buffer-health" aria-label="Physical callback performance">
                    <h2>Physical ASIO callback</h2>
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
                    <h2>One card sets the pace</h2>
                    <div className="clock-flow">
                        <div><strong>{activeClockDriver}</strong><span>Physical ASIO callbacks</span></div>
                        <span aria-hidden="true">→</span>
                        <div><strong>TimoxVasio</strong><span>Publishes virtual blocks</span></div>
                        <span aria-hidden="true">→</span>
                        <div><strong>ASIO applications</strong><span>Wake on those blocks</span></div>
                    </div>
                    <p>TimoxVasio follows the selected card's audio clock instead of running an independent timer. This avoids accumulating clock drift between its virtual stream and that card. Callback jitter can still occur, and a separate audio device can still drift.</p>
                </section>
                <section className="section performance-flow" aria-label="Audio latency path">
                    <h2>From the ASIO application to the audio output</h2>
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
                        <li><strong>Engine reads the shared ring</strong><span>At the next physical callback, the engine reads the completed block and applies the configured routes.</span>
                            <div className="stage-measurements">{measurementWindow?.clients.length ? measurementWindow.clients.map(client =>
                                <div className="stage-client" key={client.processId}>
                                    <h3>{client.driverId} · PID {client.processId}</h3>
                                    {stageDuration('Ring wait', client.ringWait)}
                                    <small>From publication to reading the same complete block; not acoustic latency.</small>
                                    <small>Discarded blocks: {client.ringWaitDiscardedBlocks}</small>
                                </div>) : <span>—</span>}</div></li>
                        <li><strong>Physical ASIO driver receives the block</strong><span>The routed output is handed to the selected card; hardware conversion adds its own delay.</span>
                            <div className="stage-measurements">
                                {stageDuration('Client read', hasApplicationPath ? measurementWindow?.physical.clientRead : null)}
                                {stageDuration('Routing', hasApplicationPath ? measurementWindow?.physical.routing : null)}
                                {stageDuration('Physical output copy', hasApplicationPath ? measurementWindow?.physical.outputCopy : null)}
                            </div></li>
                    </ol>
                    <p>If the route passes through SuperCollider, the engine first delivers a block to that application. SuperCollider then publishes its output for a following physical callback. A direct route skips this extra application stage.</p>
                    <p className="hint">At the selected rate and buffer size, one block represents {engine?.sampleRate && engine?.bufferFrames ? `${(engine.bufferFrames * 1000 / engine.sampleRate).toFixed(2)} ms` : 'an unknown duration'}. The complete application-to-speaker latency is not measured. The callback measurement above shows whether TimoxVasio finished routing each physical block within the time available.</p>
                </section>
            </main> : activeView === 'diagnostics' ? <main className="main-content api-console">
                <section className="section diagnostics-view"><div className="section-heading"><div><h2>Engine logs</h2><p className="hint">Persistent file: %LOCALAPPDATA%\TimoxVasio\logs\engine.log</p></div>
                    <div className="diagnostics-actions"><label className="field">Log level<select value={diagnostics.level} onChange={event => setDiagnosticLevel(event.target.value)}><option value="info">Standard</option><option value="debug">Detailed</option></select></label>
                        <button type="button" onClick={refreshDiagnostics} disabled={diagnosticsLoading}>{diagnosticsLoading ? 'Refreshing…' : 'Refresh'}</button>
                        {(connectionError || engine?.state !== 'running') && <button type="button" className="engine-start" onClick={() => manageEngine('start')} disabled={!!engineAction}>{engineAction === 'start' ? 'Starting…' : 'Start audio engine'}</button>}
                        {!connectionError && engine?.physicalDriverId && <button type="button" className="engine-stop" onClick={() => manageEngine('stop')} disabled={!!engineAction || (virtualDriver?.clients.length || 0) > 0} title={(virtualDriver?.clients.length || 0) > 0 ? 'Close connected ASIO applications before stopping the engine.' : undefined}>{engineAction === 'stop' ? 'Stopping…' : 'Stop audio engine'}</button>}</div></div>
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
                        {hardwareTransfer.previousConfiguration?.physicalDriverId &&
                            <button type="button" disabled={configurationLocked} onClick={restorePreviousPhysicalDriver}>
                                Restore previous driver and connections
                            </button>}
                    </div>}

                {activeView === 'configuration' && <section className="section setup-physical">
                    <div className="setup-heading"><div><span className="setup-kind">▣ Hardware</span><h2>Physical ASIO driver</h2></div>
                        <span className={`setup-state ${physicalSettingsPending ? 'pending' : 'applied'}`}>{physicalSettingsPending ? 'Changes pending' : engine?.physicalDriverId ? 'Configured' : 'No driver selected'}</span></div>
                    {engine?.physicalDriverId && <div className="analysis-actions"><button type="button" className="engine-reset"
                        disabled={!!engineAction || applying || configurationLocked}
                        onClick={() => manageEngine('reset')}>{engineAction === 'reset' ? 'Reopening driver…' : 'Reset physical driver'}</button>
                        <span className="hint">Briefly interrupts audio and reopens the last saved configuration; saved connections are kept.</span></div>}
                    {incompatiblePhysicalRoutes.length > 0 ?
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
                        disabled={!apiState || !configuration || !configurationDirty || configurationLocked || !!hardwareTransfer || incompatiblePhysicalRoutes.length > 0}>Apply configuration</button>
                    {configurationDirty && <button type="button" className="discard-draft" onClick={discardDraft}
                        disabled={configurationLocked || !!hardwareTransfer}>Discard pending edits</button>}
                </div>}

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



                {activeView === 'configuration' && <div className="analysis-actions"><button className="apply-configuration" onClick={applyConfiguration} disabled={!apiState || !configuration || configurationLocked || !!hardwareTransfer || incompatiblePhysicalRoutes.length > 0}>
                    Apply configuration
                </button>{configurationDirty && <button type="button" className="discard-draft" onClick={discardDraft}
                    disabled={configurationLocked || !!hardwareTransfer}>Discard pending edits</button>}</div>}
            </main>}
        </div>
    );
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
