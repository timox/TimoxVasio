import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import './App.css';
import './ProfessionalTheme.css';
import RoutingPatchbay from './RoutingPatchbay';
import { DEFAULT_ROUTE_COLOR, readRouteVisuals, routeVisualKey, saveRouteVisuals } from './route-visuals';
const { configurationFromState } = require('./api-contract');
const openApiDocument = require('./openapi-spec.generated');

function App() {
    const [connectionError, setConnectionError] = useState('Connecting to engine…');
    const [apiState, setApiState] = useState(null);
    const [configuration, setConfiguration] = useState(null);
    const [engine, setEngine] = useState(null);
    const [applicationProfiles, setApplicationProfiles] = useState(null);
    const [profilesLoading, setProfilesLoading] = useState(true);
    const [profilesSaving, setProfilesSaving] = useState(false);
    const [profilesError, setProfilesError] = useState('');
    const [profilesNotice, setProfilesNotice] = useState('');
    const [newProfile, setNewProfile] = useState({ processName: '', inputChannels: 256, outputChannels: 256 });
    const [notice, setNotice] = useState('');
    const [applying, setApplying] = useState(false);
    const [sourceZone, setSourceZone] = useState('virtual-output');
    const [destinationZone, setDestinationZone] = useState('physical-output');
    const [channelPageSize, setChannelPageSize] = useState(16);
    const [sourcePage, setSourcePage] = useState(0);
    const [destinationPage, setDestinationPage] = useState(0);
    const [sourceSearch, setSourceSearch] = useState('');
    const [destinationSearch, setDestinationSearch] = useState('');
    const [routingView, setRoutingView] = useState('matrix');
    const [selectedRouteId, setSelectedRouteId] = useState(null);
    const [routeVisuals, setRouteVisuals] = useState(readRouteVisuals);
    const [activeView, setActiveView] = useState('configuration');
    const [diagnostics, setDiagnostics] = useState({ level: 'info', entries: [] });
    const [diagnosticsError, setDiagnosticsError] = useState('');
    const [diagnosticsLoading, setDiagnosticsLoading] = useState(false);
    const [engineAction, setEngineAction] = useState('');
    const [meters, setMeters] = useState({});
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
    const [consoleConfirmation, setConsoleConfirmation] = useState('');
    const confirmationDialog = useRef(null);

    useEffect(() => { saveRouteVisuals(routeVisuals); }, [routeVisuals]);

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
            if (action === 'start') await window.electronAPI.startEngine();
            else await window.electronAPI.stopEngine();
            await new Promise(resolve => setTimeout(resolve, 150));
            await refreshState();
        } catch (error) { setDiagnosticsError(error.message); }
        finally { setEngineAction(''); }
    };

    const refreshState = useCallback(async () => {
        const state = await window.vasio.getState();
        setApiState(state);
        setEngine(state.engine);
        setConfiguration(configurationFromState(state));
        setSelectedStereoPairId(current => state.stereoPairs?.some(pair => pair.id === current) ? current : '');
    }, []);

    const refreshApplicationProfiles = useCallback(async () => {
        setProfilesLoading(true);
        setProfilesError('');
        try {
            const result = await window.vasio.getApplicationProfiles();
            setApplicationProfiles(result.profiles);
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
                if (message.payload.state !== 'running') setMeters({});
            }
            if (message.event === 'devices.changed') setApiState(current => current && ({ ...current, ...message.payload }));
            if (message.event === 'routes.changed') {
                refreshState().catch(error => setConnectionError(error.message));
                setMeters({});
            }
            if (message.event === 'engine.error') setNotice(message.payload.message);
            if (message.event === 'audio.meter') setMeters(current => ({ ...current, [message.payload.endpointId]: message.payload }));
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

    const endpoints = useMemo(() => {
        if (!apiState) return { all: [], sourceGroups: [], destinationGroups: [] };
        const physical = apiState.physicalDrivers;
        const virtual = apiState.virtualDrivers;
        const physicalInputs = physical.flatMap(driver => driver.inputEndpoints);
        const physicalOutputs = physical.flatMap(driver => driver.outputEndpoints);
        const virtualInputs = virtual.flatMap(driver => driver.inputEndpoints);
        const virtualOutputs = virtual.flatMap(driver => driver.outputEndpoints);
        return {
            all: [...physical.flatMap(driver => [...driver.inputEndpoints, ...driver.outputEndpoints]),
                ...virtual.flatMap(driver => [...driver.inputEndpoints, ...driver.outputEndpoints])],
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
    }, [apiState]);
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
        return {
            sources: [
                ...virtualGroups('output'),
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
                ...virtualGroups('input')
            ]
        };
    }, [apiState]);
    const activeRouteKeys = useMemo(() => new Set((configuration?.routes || []).map(route =>
        `${route.sourceEndpointId}\u0000${route.destinationEndpointId}`)), [configuration?.routes]);
    const sourceGroup = endpoints.sourceGroups.find(group => group.id === sourceZone);
    const destinationGroup = endpoints.destinationGroups.find(group => group.id === destinationZone);
    const pageEndpoints = (group, search) => (group?.endpoints || []).filter(endpoint =>
        endpoint.name.toLocaleLowerCase().includes(search.trim().toLocaleLowerCase()));
    const matchingSources = pageEndpoints(sourceGroup, sourceSearch);
    const matchingDestinations = pageEndpoints(destinationGroup, destinationSearch);
    const visibleSources = matchingSources.slice(sourcePage * channelPageSize, (sourcePage + 1) * channelPageSize);
    const visibleDestinations = matchingDestinations.slice(destinationPage * channelPageSize, (destinationPage + 1) * channelPageSize);
    const visibleSourceIds = new Set(visibleSources.map(endpoint => endpoint.id));
    const visibleDestinationIds = new Set(visibleDestinations.map(endpoint => endpoint.id));
    const visibleRouteCount = (configuration?.routes || []).filter(route =>
        visibleSourceIds.has(route.sourceEndpointId) && visibleDestinationIds.has(route.destinationEndpointId)).length;
    const sourcePageCount = Math.max(1, Math.ceil(matchingSources.length / channelPageSize));
    const destinationPageCount = Math.max(1, Math.ceil(matchingDestinations.length / channelPageSize));
    const routedSources = new Set((configuration?.routes || []).map(route => route.sourceEndpointId));
    const routedDestinations = new Set((configuration?.routes || []).map(route => route.destinationEndpointId));

    useEffect(() => {
        if (!['physical-input', 'virtual-output'].includes(sourceZone)) setSourceZone('virtual-output');
        const allowed = sourceZone === 'physical-input' ? ['virtual-input'] : ['physical-output', 'virtual-input'];
        if (!allowed.includes(destinationZone)) setDestinationZone(allowed[0]);
    }, [sourceZone, destinationZone]);
    useEffect(() => { setSourcePage(page => Math.min(page, sourcePageCount - 1)); }, [sourcePageCount]);
    useEffect(() => { setDestinationPage(page => Math.min(page, destinationPageCount - 1)); }, [destinationPageCount]);

    const showRouteInMatrix = route => {
        setRoutingView('matrix');
        const source = endpoints.all.find(endpoint => endpoint.id === route.sourceEndpointId);
        const destination = endpoints.all.find(endpoint => endpoint.id === route.destinationEndpointId);
        if (!source || !destination) return;
        const sourceGroupId = endpoints.sourceGroups.find(group => group.endpoints.some(endpoint => endpoint.id === source.id))?.id;
        const destinationGroupId = endpoints.destinationGroups.find(group => group.endpoints.some(endpoint => endpoint.id === destination.id))?.id;
        if (!sourceGroupId || !destinationGroupId) return;
        setSourceZone(sourceGroupId);
        setDestinationZone(destinationGroupId);
        setSourceSearch('');
        setDestinationSearch('');
        const newSourceGroup = endpoints.sourceGroups.find(group => group.id === sourceGroupId);
        const newDestinationGroup = endpoints.destinationGroups.find(group => group.id === destinationGroupId);
        setSourcePage(Math.floor(newSourceGroup.endpoints.findIndex(endpoint => endpoint.id === source.id) / channelPageSize));
        setDestinationPage(Math.floor(newDestinationGroup.endpoints.findIndex(endpoint => endpoint.id === destination.id) / channelPageSize));
    };

    const configurationLocked = applying || engine?.state === 'reconfiguring';
    const updateDraft = (update) => setConfiguration(current =>
        configurationLocked || !current ? current : update(current));
    const updateConfiguration = (field, value) => updateDraft(current => ({ ...current, [field]: value }));
    const toggleRoute = (sourceEndpointId, destinationEndpointId) => setConfiguration(current => {
        if (configurationLocked || !current) return current;
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
    const editRoute = (id, field, value) => updateDraft(current => ({
        ...current, routes: current.routes.map(route => route.id === id ? { ...route, [field]: value } : route)
    }));
    const removeRoute = id => updateDraft(current => ({
        ...current, routes: current.routes.filter(route => route.id !== id)
    }));
    const updateApplicationProfile = (index, field, value) => setApplicationProfiles(current =>
        current.map((profile, profileIndex) => profileIndex === index
            ? { ...profile, [field]: value } : profile));
    const addApplicationProfile = () => {
        setProfilesError('');
        setProfilesNotice('');
        setApplicationProfiles(current => [...current, newProfile]);
        setNewProfile({ processName: '', inputChannels: 256, outputChannels: 256 });
    };
    const removeApplicationProfile = index => setApplicationProfiles(current =>
        current.filter((_profile, profileIndex) => profileIndex !== index));
    const saveApplicationProfiles = async () => {
        if (!applicationProfiles || profilesLoading || profilesSaving) return;
        setProfilesSaving(true);
        setProfilesError('');
        setProfilesNotice('');
        try {
            const result = await window.vasio.replaceApplicationProfiles(applicationProfiles);
            setApplicationProfiles(result.profiles);
            const clients = result.restartRequiredClients;
            setProfilesNotice(clients.length
                ? `Profiles saved. Restart ${clients.map(client => `${client.processName} (PID ${client.pid})`).join(', ')} to apply the changes.`
                : 'Profiles saved. New ASIO instances will use these values.');
        } catch (error) {
            setProfilesError(error.status === 404
                ? 'The running engine does not support application profiles yet. Rebuild and restart TimoxVasio Engine.'
                : error.message);
        } finally {
            setProfilesSaving(false);
        }
    };
    const applyConfiguration = async () => {
        if (!configuration || configurationLocked) return;
        setApplying(true);
        setNotice('Applying configuration…');
        try {
            await window.vasio.applyConfiguration(configuration);
            setNotice(configuration.routes.length === 0
                ? 'Physical driver configured. The audio engine remains stopped until a route is defined.'
                : 'Configuration applied. Audio routing was briefly interrupted.');
            await refreshState();
        } catch (error) {
            setNotice(error.message);
        } finally {
            setApplying(false);
        }
    };

    const endpointName = id => endpoints.all.find(item => item.id === id)?.name || id;
    const physicalDrivers = apiState?.physicalDrivers || [];
    const selectedPhysical = physicalDrivers.find(driver => driver.id === configuration?.physicalDriverId);
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

    const startCorrelation = async () => {
        if (!selectedStereoPairId) return;
        try { await window.vasio.startCorrelation(selectedStereoPairId); setCorrelationHistory([]); }
        catch (error) { setNotice(error.message); }
    };
    const configurationDirty = !!(apiState && configuration &&
        JSON.stringify(configuration) !== JSON.stringify(configurationFromState(apiState)));
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
        <div className="App">
            <header className="App-header">
                <div className="brand"><h1><a href="https://github.com/timox/TimoxVasio" target="_blank" rel="noopener noreferrer" title="Open the TimoxVasio GitHub repository">Timox VASIO Control</a></h1></div>
                <div className="status"><span className={`status-light ${engine?.state || 'disconnected'}`} />
                    {connectionError || `Engine: ${engine?.state || 'pending'}`}</div>
            </header>
            <nav className="view-tabs" aria-label="Application sections">
                {[['configuration', 'Configuration'], ['channels', 'Channels'], ['analysis', 'Analysis'], ['api', 'API and Swagger'], ['diagnostics', 'Logs']].map(([id, label]) =>
                    <button key={id} type="button" className={activeView === id ? 'selected' : ''} aria-current={activeView === id ? 'page' : undefined}
                        onClick={() => { setActiveView(id); if (id === 'diagnostics') refreshDiagnostics(); }}>{label}</button>)}
            </nav>
            {activeView === 'analysis' ? <main className="main-content api-console">
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
            </main> : activeView === 'diagnostics' ? <main className="main-content api-console">
                <section className="section diagnostics-view"><div className="section-heading"><div><h2>Engine logs</h2><p className="hint">Persistent file: %LOCALAPPDATA%\TimoxVasio\logs\engine.log</p></div>
                    <div className="diagnostics-actions"><label className="field">Log level<select value={diagnostics.level} onChange={event => setDiagnosticLevel(event.target.value)}><option value="info">Standard</option><option value="debug">Detailed</option></select></label>
                        <button type="button" onClick={refreshDiagnostics} disabled={diagnosticsLoading}>{diagnosticsLoading ? 'Refreshing…' : 'Refresh'}</button>
                        {connectionError || engine?.state !== 'running' ? <button type="button" className="engine-start" onClick={() => manageEngine('start')} disabled={!!engineAction}>{engineAction === 'start' ? 'Starting…' : 'Start audio engine'}</button> :
                            <button type="button" className="engine-stop" onClick={() => manageEngine('stop')} disabled={!!engineAction || (virtualDriver?.clients.length || 0) > 0} title={(virtualDriver?.clients.length || 0) > 0 ? 'Close connected ASIO applications before stopping the engine.' : undefined}>{engineAction === 'stop' ? 'Stopping…' : 'Stop audio engine'}</button>}</div></div>
                    {diagnosticsError && <div className="error-panel">{diagnosticsError}</div>}
                    <div className="log-table-wrap"><table className="log-table"><thead><tr><th>UTC time</th><th>Level</th><th>Component</th><th>Message</th></tr></thead>
                        <tbody>{diagnostics.entries.map((entry, index) => <tr key={`${entry.timestamp}-${index}`}><td>{entry.timestamp}</td><td><span className={`log-level ${entry.level}`}>{entry.level}</span></td><td>{entry.component}</td><td>{entry.message}</td></tr>)}
                            {!diagnostics.entries.length && <tr><td colSpan="4" className="empty-log">No log entries recorded.</td></tr>}</tbody></table></div>
                </section>
            </main> : <main className="main-content api-console">
                {engine?.lastError && <div className="error-panel">{engine.lastError.message}</div>}
                {notice && <div className="notice" role="status">{notice}</div>}

                {activeView === 'configuration' && <section className="section">
                    <h2>Virtual ASIO drivers</h2>
                    {!virtualDriver ? <p className="hint">The engine has not published a virtual driver.</p> : <div className="driver-settings">
                        <article className="driver-setting">
                            <h3>{virtualDriver.id}</h3>
                            <p>Maximum per application: {virtualDriver.channelsPerDirection} inputs · {virtualDriver.channelsPerDirection} outputs</p>
                            <p>Clock: {engine?.physicalDriverId ? 'selected physical driver' : 'waiting for physical driver'}</p>
                            <p>Sample rate: {virtualDriver.sampleRate ? `${virtualDriver.sampleRate} Hz` : 'pending'}</p>
                            <p>Block size: {virtualDriver.bufferFrames ? `${virtualDriver.bufferFrames} frames` : 'pending'}</p>
                        </article>
                    </div>}
                    <p className="hint">Use the “Physical ASIO driver” menu below to select the master clock.</p>
                </section>}

                {activeView === 'configuration' && <section className="section">
                    <h2>Application channel profiles</h2>
                    <p className="hint">These values limit the channels advertised to each executable. They do not reduce the 256 transport channels. Changes take effect when the ASIO application restarts.</p>
                    {profilesError && <div className="error-panel" role="alert">{profilesError}</div>}
                    {profilesNotice && <div className="notice" role="status">{profilesNotice}</div>}
                    {profilesLoading ? <p className="hint">Loading profiles from the API…</p> : applicationProfiles && <>
                        <div className="profile-table-scroll">
                            <table className="application-profile-table">
                                <thead><tr><th>Executable</th><th>Input channels</th><th>Output channels</th><th>Action</th></tr></thead>
                                <tbody>
                                    {applicationProfiles.map((profile, index) => <tr key={`${profile.processName}-${index}`}>
                                        <td><input aria-label={`Executable ${index + 1}`} value={profile.processName}
                                            onChange={event => updateApplicationProfile(index, 'processName', event.target.value)} /></td>
                                        <td><input aria-label={`Input channels ${index + 1}`} type="number" min="1" max="256" step="1"
                                            value={profile.inputChannels} onChange={event => updateApplicationProfile(index, 'inputChannels', Number(event.target.value))} /></td>
                                        <td><input aria-label={`Output channels ${index + 1}`} type="number" min="1" max="256" step="1"
                                            value={profile.outputChannels} onChange={event => updateApplicationProfile(index, 'outputChannels', Number(event.target.value))} /></td>
                                        <td><button type="button" onClick={() => removeApplicationProfile(index)} aria-label={`Remove profile ${profile.processName}`}>Remove</button></td>
                                    </tr>)}
                                    {!applicationProfiles.length && <tr><td colSpan="4">No custom profiles; the default is 256 inputs and 256 outputs.</td></tr>}
                                </tbody>
                            </table>
                        </div>
                        <div className="profile-add-row">
                            <label className="field">New executable
                                <input value={newProfile.processName} placeholder="example.exe" onChange={event => setNewProfile(current => ({ ...current, processName: event.target.value }))} />
                            </label>
                            <label className="field">Inputs
                                <input type="number" min="1" max="256" step="1" value={newProfile.inputChannels}
                                    onChange={event => setNewProfile(current => ({ ...current, inputChannels: Number(event.target.value) }))} />
                            </label>
                            <label className="field">Outputs
                                <input type="number" min="1" max="256" step="1" value={newProfile.outputChannels}
                                    onChange={event => setNewProfile(current => ({ ...current, outputChannels: Number(event.target.value) }))} />
                            </label>
                            <button type="button" onClick={addApplicationProfile} disabled={!newProfile.processName.trim()}>Add profile</button>
                        </div>
                        <button type="button" className="save-profiles" aria-label="Save profiles"
                            disabled={profilesSaving} onClick={saveApplicationProfiles}>
                            {profilesSaving ? 'Saving…' : 'Save profiles'}
                        </button>
                    </>}
                </section>}

                {activeView === 'channels' && <section className="section">
                    <h2>Routing</h2>
                    <details className="routing-details" open><summary>Edit routes · {(configuration?.routes || []).length} configured</summary>
                    <div className="routing-view-switch" role="group" aria-label="Routing view">
                        <button type="button" data-routing-view="patchbay" aria-pressed={routingView === 'patchbay'}
                            className={routingView === 'patchbay' ? 'selected' : ''} onClick={() => setRoutingView('patchbay')}>Connections</button>
                        <button type="button" data-routing-view="matrix" aria-pressed={routingView === 'matrix'}
                            className={routingView === 'matrix' ? 'selected' : ''} onClick={() => setRoutingView('matrix')}>Matrix</button>
                    </div>
                    {routingView === 'patchbay' ? <RoutingPatchbay sourceGroups={patchbayGroups.sources}
                        destinationGroups={patchbayGroups.destinations} routes={configuration?.routes || []}
                        onToggleRoute={toggleRoute} selectedRouteId={selectedRouteId}
                        routeVisuals={routeVisuals} onChangeRouteVisual={updateRouteVisual}
                        onBulkChangeRouteVisuals={updateManyRouteVisuals}
                        onSelectRoute={setSelectedRouteId} onEditRoute={editRoute}
                        disabled={configurationLocked} /> : <>
                    <p className="hint">Select a source zone and a compatible destination zone. Each cell connects its row channel to its column channel. Click to add or remove a route.</p>
                    <div className="routing-matrix">
                        <div className="matrix-controls">
                            <label className="field">Source zone
                                <select value={sourceZone} onChange={event => { setSourceZone(event.target.value); setSourcePage(0); setSourceSearch(''); }}>
                                    <option value="virtual-output">Virtual outputs (applications → engine)</option>
                                    <option value="physical-input">Physical inputs (driver → engine)</option>
                                </select>
                            </label>
                            <label className="field">Destination zone
                                <select value={destinationZone} onChange={event => { setDestinationZone(event.target.value); setDestinationPage(0); setDestinationSearch(''); }}>
                                    {(sourceZone === 'physical-input' ? ['virtual-input'] : ['physical-output', 'virtual-input']).map(id => {
                                        const group = endpoints.destinationGroups.find(item => item.id === id);
                                        return <option key={id} value={id}>{group?.label}</option>;
                                    })}
                                </select>
                            </label>
                            <label className="field matrix-page-size">Channels per page
                                <select value={channelPageSize} onChange={event => { setChannelPageSize(Number(event.target.value)); setSourcePage(0); setDestinationPage(0); }}>
                                    {[8, 16, 32].map(size => <option key={size} value={size}>{size}</option>)}
                                </select>
                            </label>
                        </div>
                        <div className="matrix-search-row">
                            <label>Search source channels<input value={sourceSearch} onChange={event => { setSourceSearch(event.target.value); setSourcePage(0); }} placeholder="Channel name" /></label>
                            <label>Search destination channels<input value={destinationSearch} onChange={event => { setDestinationSearch(event.target.value); setDestinationPage(0); }} placeholder="Channel name" /></label>
                        </div>
                        <div className="matrix-axis-summary">
                            <span>{sourceGroup?.label || 'Source'} · {matchingSources.length} channels</span>
                            <span>{destinationGroup?.label || 'Destination'} · {matchingDestinations.length} channels</span>
                            <span className={`route-legend${visibleRouteCount === 0 ? ' empty' : ''}`}>
                                {visibleRouteCount === 0 ? 'No routes in this view' : <><i aria-hidden="true">●</i> {visibleRouteCount} {visibleRouteCount === 1 ? 'route' : 'routes'} in this view</>}
                            </span>
                        </div>
                        <div className="matrix-scroll" role="region" aria-label="Paginated routing matrix" tabIndex="0">
                            {!visibleSources.length || !visibleDestinations.length ? <div className="matrix-empty">
                                <strong>{!sourceGroup?.endpoints.length || !destinationGroup?.endpoints.length ? 'No channels published in this zone.' : 'No channels match the search.'}</strong>
                                <p>{!sourceGroup?.endpoints.length || !destinationGroup?.endpoints.length ? 'Check active drivers and channels published by the API.' : 'Change or clear the search text.'}</p>
                            </div> : <table className="route-matrix">
                                <thead>
                                    <tr className="matrix-zone-headings">
                                        <th className="matrix-corner" scope="col">Sources ↓ / Destinations →</th>
                                        <th colSpan={visibleDestinations.length} scope="colgroup">{destinationGroup.label}</th>
                                    </tr>
                                    <tr className="matrix-endpoint-headings">
                                        <th className="matrix-corner matrix-corner-sub" scope="col">{sourceGroup.label}</th>
                                        {visibleDestinations.map(endpoint => <th key={endpoint.id} className={routedDestinations.has(endpoint.id) ? 'matrix-configured' : ''} scope="col" title={endpoint.name}>
                                            <span>{routedDestinations.has(endpoint.id) ? '● ' : ''}{endpoint.name}</span>
                                        </th>)}
                                    </tr>
                                </thead>
                                <tbody>
                                    {visibleSources.map(source => <tr key={source.id}>
                                        <th className={`matrix-source-endpoint${routedSources.has(source.id) ? ' matrix-configured' : ''}`} scope="row" title={source.name}>
                                            {routedSources.has(source.id) ? <span className="configured-mark" aria-label="Routed source">● </span> : null}{source.name}
                                        </th>
                                        {visibleDestinations.map(destination => {
                                            const route = activeRouteKeys.has(`${source.id}\u0000${destination.id}`);
                                            return <td key={destination.id}>
                                                <button type="button" className={`route-cell${route ? ' active' : ''}`}
                                                    disabled={configurationLocked}
                                                    aria-label={`${source.name} to ${destination.name}${route ? ', route configured' : ', route inactive'}`}
                                                    aria-pressed={route} title={`${source.name} → ${destination.name}`}
                                                    onClick={() => toggleRoute(source.id, destination.id)}>
                                                    {route ? '●' : ''}
                                                </button>
                                            </td>;
                                        })}
                                    </tr>)}
                                </tbody>
                            </table>}
                        </div>
                        <div className="matrix-pagers">
                            <div className="matrix-pager">
                                <span>Sources : {matchingSources.length ? sourcePage * channelPageSize + 1 : 0}–{Math.min((sourcePage + 1) * channelPageSize, matchingSources.length)} / {matchingSources.length}</span>
                                <button type="button" onClick={() => setSourcePage(page => Math.max(0, page - 1))} disabled={sourcePage === 0}>Previous</button>
                                <button type="button" onClick={() => setSourcePage(page => Math.min(sourcePageCount - 1, page + 1))} disabled={sourcePage >= sourcePageCount - 1}>Next</button>
                            </div>
                            <div className="matrix-pager">
                                <span>Destinations : {matchingDestinations.length ? destinationPage * channelPageSize + 1 : 0}–{Math.min((destinationPage + 1) * channelPageSize, matchingDestinations.length)} / {matchingDestinations.length}</span>
                                <button type="button" onClick={() => setDestinationPage(page => Math.max(0, page - 1))} disabled={destinationPage === 0}>Previous</button>
                                <button type="button" onClick={() => setDestinationPage(page => Math.min(destinationPageCount - 1, page + 1))} disabled={destinationPage >= destinationPageCount - 1}>Next</button>
                            </div>
                        </div>
                    </div>
                    </>}
                    {(configuration?.routes || []).length === 0 ? <p className="hint">No routes configured.</p> : <ul className="configured-routes">
                        {(configuration?.routes || []).map(route => <li key={route.id} className={`custom-route-row${selectedRouteId === route.id ? ' route-highlighted' : ''}`}
                            style={{ '--route-color': routeVisuals[routeVisualKey(route)]?.color || DEFAULT_ROUTE_COLOR }}>
                            <span className="route-list-description">
                                {routeVisuals[routeVisualKey(route)]?.label && <strong className="route-custom-label">{routeVisuals[routeVisualKey(route)].label}</strong>}
                                {endpointName(route.sourceEndpointId)} → {endpointName(route.destinationEndpointId)}
                            </span>
                            <button type="button" className="highlight-route" aria-pressed={selectedRouteId === route.id}
                                onClick={() => { setRoutingView('patchbay'); setSelectedRouteId(current => current === route.id ? null : route.id); }}>
                                {selectedRouteId === route.id ? 'Clear highlight' : 'Highlight connection'}
                            </button>
                            <button type="button" onClick={() => showRouteInMatrix(route)}>Show in matrix</button>
                            <label>Gain (dB) <input disabled={configurationLocked} type="number" min="-120" max="24" step="0.1" value={route.gainDb} onChange={event => editRoute(route.id, 'gainDb', Number(event.target.value))} /></label>
                            <label><input disabled={configurationLocked} type="checkbox" checked={route.mute} onChange={event => editRoute(route.id, 'mute', event.target.checked)} /> Muted</label>
                            <button disabled={configurationLocked} onClick={() => removeRoute(route.id)} aria-label="Remove route">Remove</button>
                        </li>)}
                    </ul>}
                    </details>
                </section>}
                {activeView === 'channels' && <div className="apply-bar">
                    <span>{configurationDirty ? 'Pending changes · applying briefly interrupts audio routing.' : 'No pending changes.'}</span>
                    <button className="apply-configuration" onClick={applyConfiguration}
                        disabled={!apiState || !configuration || configurationLocked}>Apply configuration</button>
                </div>}

                {activeView === 'channels' && <section className="section">
                    <h2>Application channels</h2>
                    <p className="hint">“Open” shows channels advertised by the application. “Routed” shows channels connected in the displayed configuration. Open channels are not routed automatically.</p>
                    {applicationChannelSummaries.length === 0 ? <p className="hint">No applications connected to TimoxVasio.</p> :
                        <div className="application-channel-list">
                            {applicationChannelSummaries.map(client => <article className="application-channel-card" key={client.pid}>
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
                </section>}

                {activeView === 'configuration' && <section className="section">
                    <h2>Physical ASIO driver</h2>
                    <label className="field">Selected driver
                        <select disabled={configurationLocked} value={configuration?.physicalDriverId || ''} onChange={event => updateDraft(current => ({
                            ...current,
                            physicalDriverId: event.target.value || null,
                            sampleRate: null,
                            bufferFrames: null
                        }))}>
                            <option value="">None (engine stopped)</option>
                            {physicalDrivers.map(driver => <option key={driver.id} value={driver.id}>{driver.name}</option>)}
                        </select>
                    </label>
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

                {activeView === 'channels' && <section className="section">
                    <h2>Clients connected to TimoxVasio</h2>
                    {!virtualDriver?.clients.length ? <p className="hint">No ASIO clients connected.</p> : <ul className="configured-routes">
                        {virtualDriver.clients.map(client => <li key={client.pid}>
                            <span>{client.processName} · PID {client.pid}</span>
                            <span>Inputs: {client.inputChannels.join(', ') || 'none'}</span>
                            <span>Outputs: {client.outputChannels.join(', ') || 'none'}</span>
                        </li>)}
                    </ul>}
                </section>}

                {activeView === 'configuration' && <button className="apply-configuration" onClick={applyConfiguration} disabled={!apiState || !configuration || configurationLocked}>
                    Apply configuration
                </button>}
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
