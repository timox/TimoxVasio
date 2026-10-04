import React, { useCallback, useEffect, useMemo, useState } from 'react';
import './App.css';
const { configurationFromState } = require('./api-contract');

function App() {
    const [connectionError, setConnectionError] = useState('Connexion au moteur…');
    const [apiState, setApiState] = useState(null);
    const [configuration, setConfiguration] = useState(null);
    const [engine, setEngine] = useState(null);
    const [notice, setNotice] = useState('');
    const [applying, setApplying] = useState(false);
    const [sourceZone, setSourceZone] = useState('virtual-output');
    const [destinationZone, setDestinationZone] = useState('physical-output');
    const [channelPageSize, setChannelPageSize] = useState(16);
    const [sourcePage, setSourcePage] = useState(0);
    const [destinationPage, setDestinationPage] = useState(0);
    const [sourceSearch, setSourceSearch] = useState('');
    const [destinationSearch, setDestinationSearch] = useState('');

    const refreshState = useCallback(async () => {
        const state = await window.vasio.getState();
        setApiState(state);
        setEngine(state.engine);
        setConfiguration(configurationFromState(state));
    }, []);

    useEffect(() => {
        let disposed = false;
        let disconnectListener;
        let diagnosticListener;
        if (!window.vasio || typeof window.vasio.subscribeEvents !== 'function') {
            setConnectionError('Lancez TimoxVasio Control pour accéder au moteur audio.');
            return undefined;
        }
        const unsubscribeEvents = window.vasio.subscribeEvents(message => {
            if (message.event === 'engine.status') setEngine(message.payload);
            if (message.event === 'devices.changed') setApiState(current => current && ({ ...current, ...message.payload }));
            if (message.event === 'routes.changed') {
                setApiState(current => current && ({ ...current, routes: message.payload.routes }));
                setConfiguration(current => current && ({ ...current, routes: message.payload.routes }));
            }
            if (message.event === 'engine.error') setNotice(message.payload.message);
        });
        const connect = async () => {
            try {
                await refreshState();
                if (disposed) return;
                setConnectionError('');
                disconnectListener = window.electronAPI.onEngineConnection(status => {
                    if (!status.connected) setConnectionError(status.error || 'Le moteur VASIO est arrêté');
                });
                diagnosticListener = window.electronAPI.onEngineDiagnostic(message => setNotice(message));
                const unsubscribeDisconnect = window.vasio.onDisconnect(() => {
                    if (!disposed) setConnectionError('Connexion à l’API interrompue');
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
    }, [refreshState]);

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
                { id: 'physical-input', label: 'Entrées physiques', endpoints: physicalInputs },
                { id: 'physical-output', label: 'Sorties physiques', endpoints: physicalOutputs },
                { id: 'virtual-input', label: 'Entrées virtuelles', endpoints: virtualInputs },
                { id: 'virtual-output', label: 'Sorties virtuelles', endpoints: virtualOutputs }
            ],
            destinationGroups: [
                { id: 'physical-input', label: 'Entrées physiques', endpoints: physicalInputs },
                { id: 'physical-output', label: 'Sorties physiques', endpoints: physicalOutputs },
                { id: 'virtual-input', label: 'Entrées virtuelles', endpoints: virtualInputs },
                { id: 'virtual-output', label: 'Sorties virtuelles', endpoints: virtualOutputs }
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
    const applyConfiguration = async () => {
        if (!configuration || configurationLocked) return;
        setApplying(true);
        setNotice('Application de la configuration…');
        try {
            await window.vasio.applyConfiguration(configuration);
            setNotice(configuration.routes.length === 0
                ? 'Pilote physique configuré. Le moteur audio reste arrêté tant qu’aucune route n’est définie.'
                : 'Configuration appliquée. Le routage audio a été interrompu pendant le changement.');
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
            routedOutputCount: routedOutputs.size
        };
    });

    return (
        <div className="App">
            <header className="App-header">
                <h1>VASIO Control</h1>
                <div className="status"><span className={`status-light ${engine?.state || 'disconnected'}`} />
                    {connectionError || `Moteur : ${engine?.state || 'en attente'}`}</div>
            </header>
            <main className="main-content api-console">
                {engine?.lastError && <div className="error-panel">{engine.lastError.message}</div>}
                {notice && <div className="notice" role="status">{notice}</div>}

                <section className="section">
                    <h2>Pilotes ASIO virtuels</h2>
                    {!virtualDriver ? <p className="hint">Aucun pilote virtuel publié par le moteur.</p> : <div className="driver-settings">
                        <article className="driver-setting">
                            <h3>{virtualDriver.id}</h3>
                            <p>Capacité maximale par application : {virtualDriver.channelsPerDirection} entrées · {virtualDriver.channelsPerDirection} sorties</p>
                            <p>Horloge : {engine?.physicalDriverId ? 'pilote physique sélectionné' : 'en attente du pilote physique'}</p>
                            <p>Fréquence : {virtualDriver.sampleRate ? `${virtualDriver.sampleRate} Hz` : 'en attente'}</p>
                            <p>Taille de bloc : {virtualDriver.bufferFrames ? `${virtualDriver.bufferFrames} frames` : 'en attente'}</p>
                        </article>
                    </div>}
                    <p className="hint">Le menu « Pilote ASIO physique » ci-dessous sert uniquement à choisir l’horloge maîtresse.</p>
                </section>

                <section className="section">
                    <h2>Canaux des applications</h2>
                    <p className="hint">« Ouverts » indique les canaux annoncés par l’application. « Avec une route » indique ceux reliés dans la configuration affichée. Les canaux ouverts ne sont pas automatiquement routés.</p>
                    {applicationChannelSummaries.length === 0 ? <p className="hint">Aucune application connectée à TimoxVasio.</p> :
                        <div className="application-channel-list">
                            {applicationChannelSummaries.map(client => <article className="application-channel-card" key={client.pid}>
                                <h3>{client.processName}</h3>
                                <div className="application-channel-directions">
                                    <div><strong>Entrées</strong><span>{client.inputChannels.length} ouvertes</span><span>{client.routedInputCount} avec une route</span></div>
                                    <div><strong>Sorties</strong><span>{client.outputChannels.length} ouvertes</span><span>{client.routedOutputCount} avec une route</span></div>
                                </div>
                            </article>)}
                        </div>}
                </section>

                <section className="section">
                    <h2>Pilote ASIO physique</h2>
                    <label className="field">Pilote sélectionné
                        <select disabled={configurationLocked} value={configuration?.physicalDriverId || ''} onChange={event => updateDraft(current => ({
                            ...current,
                            physicalDriverId: event.target.value || null,
                            sampleRate: null,
                            bufferFrames: null
                        }))}>
                            <option value="">Aucun (moteur arrêté)</option>
                            {physicalDrivers.map(driver => <option key={driver.id} value={driver.id}>{driver.name}</option>)}
                        </select>
                    </label>
                    {configuration?.physicalDriverId && <>
                        <label className="field">Fréquence physique
                            <select disabled={configurationLocked} value={configuration.sampleRate || ''} onChange={event => updateConfiguration('sampleRate', event.target.value ? Number(event.target.value) : null)}>
                                <option value="">Taux courant du pilote</option>
                                {availableRates.map(rate => <option key={rate} value={rate}>{rate} Hz</option>)}
                            </select>
                        </label>
                        <label className="field">Taille de buffer physique
                            <select disabled={configurationLocked} value={configuration.bufferFrames || ''} onChange={event => updateConfiguration('bufferFrames', event.target.value ? Number(event.target.value) : null)}>
                                <option value="">Taille préférée du pilote</option>
                                {bufferSizes.map(frames => <option key={frames} value={frames}>{frames} frames</option>)}
                            </select>
                        </label>
                        <p className="hint">Les ports physiques sont découverts après l’application de ces paramètres.</p>
                    </>}
                </section>

                <section className="section">
                    <h2>Clients connectés à TimoxVasio</h2>
                    {!virtualDriver?.clients.length ? <p className="hint">Aucun client ASIO connecté.</p> : <ul className="configured-routes">
                        {virtualDriver.clients.map(client => <li key={client.pid}>
                            <span>{client.processName} · PID {client.pid}</span>
                            <span>Entrées : {client.inputChannels.join(', ') || 'aucune'}</span>
                            <span>Sorties : {client.outputChannels.join(', ') || 'aucune'}</span>
                        </li>)}
                    </ul>}
                </section>

                <section className="section">
                    <h2>Routage</h2>
                    <p className="hint">Choisissez une zone source et une zone de destination autorisée. Une case relie le canal de sa ligne au canal de sa colonne; cliquez pour ajouter ou retirer la route.</p>
                    <div className="routing-matrix">
                        <div className="matrix-controls">
                            <label className="field">Zone source
                                <select value={sourceZone} onChange={event => { setSourceZone(event.target.value); setSourcePage(0); setSourceSearch(''); }}>
                                    <option value="virtual-output">Sorties virtuelles (applications → moteur)</option>
                                    <option value="physical-input">Entrées physiques (pilote → moteur)</option>
                                </select>
                            </label>
                            <label className="field">Zone destination
                                <select value={destinationZone} onChange={event => { setDestinationZone(event.target.value); setDestinationPage(0); setDestinationSearch(''); }}>
                                    {(sourceZone === 'physical-input' ? ['virtual-input'] : ['physical-output', 'virtual-input']).map(id => {
                                        const group = endpoints.destinationGroups.find(item => item.id === id);
                                        return <option key={id} value={id}>{group?.label}</option>;
                                    })}
                                </select>
                            </label>
                            <label className="field matrix-page-size">Canaux par page
                                <select value={channelPageSize} onChange={event => { setChannelPageSize(Number(event.target.value)); setSourcePage(0); setDestinationPage(0); }}>
                                    {[8, 16, 32].map(size => <option key={size} value={size}>{size}</option>)}
                                </select>
                            </label>
                        </div>
                        <div className="matrix-search-row">
                            <label>Rechercher un canal source<input value={sourceSearch} onChange={event => { setSourceSearch(event.target.value); setSourcePage(0); }} placeholder="Nom du canal" /></label>
                            <label>Rechercher un canal destination<input value={destinationSearch} onChange={event => { setDestinationSearch(event.target.value); setDestinationPage(0); }} placeholder="Nom du canal" /></label>
                        </div>
                        <div className="matrix-axis-summary">
                            <span>{sourceGroup?.label || 'Source'} · {matchingSources.length} canaux</span>
                            <span>{destinationGroup?.label || 'Destination'} · {matchingDestinations.length} canaux</span>
                            <span className="route-legend"><i aria-hidden="true">●</i> Route configurée</span>
                        </div>
                        <div className="matrix-scroll" role="region" aria-label="Matrice paginée de routage" tabIndex="0">
                            {!visibleSources.length || !visibleDestinations.length ? <div className="matrix-empty">
                                <strong>{!sourceGroup?.endpoints.length || !destinationGroup?.endpoints.length ? 'Aucun canal publié dans cette zone.' : 'Aucun canal ne correspond à la recherche.'}</strong>
                                <p>{!sourceGroup?.endpoints.length || !destinationGroup?.endpoints.length ? 'Vérifiez les pilotes actifs et les canaux publiés par l’API.' : 'Modifiez ou effacez le texte de recherche.'}</p>
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
                                            {routedSources.has(source.id) ? <span className="configured-mark" aria-label="Source routée">● </span> : null}{source.name}
                                        </th>
                                        {visibleDestinations.map(destination => {
                                            const route = activeRouteKeys.has(`${source.id}\u0000${destination.id}`);
                                            return <td key={destination.id}>
                                                <button type="button" className={`route-cell${route ? ' active' : ''}`}
                                                    disabled={configurationLocked}
                                                    aria-label={`${source.name} vers ${destination.name}${route ? ', route configurée' : ', route inactive'}`}
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
                                <button type="button" onClick={() => setSourcePage(page => Math.max(0, page - 1))} disabled={sourcePage === 0}>Précédent</button>
                                <button type="button" onClick={() => setSourcePage(page => Math.min(sourcePageCount - 1, page + 1))} disabled={sourcePage >= sourcePageCount - 1}>Suivant</button>
                            </div>
                            <div className="matrix-pager">
                                <span>Destinations : {matchingDestinations.length ? destinationPage * channelPageSize + 1 : 0}–{Math.min((destinationPage + 1) * channelPageSize, matchingDestinations.length)} / {matchingDestinations.length}</span>
                                <button type="button" onClick={() => setDestinationPage(page => Math.max(0, page - 1))} disabled={destinationPage === 0}>Précédent</button>
                                <button type="button" onClick={() => setDestinationPage(page => Math.min(destinationPageCount - 1, page + 1))} disabled={destinationPage >= destinationPageCount - 1}>Suivant</button>
                            </div>
                        </div>
                    </div>
                    {(configuration?.routes || []).length === 0 ? <p className="hint">Aucune route configurée.</p> : <ul className="configured-routes">
                        {(configuration?.routes || []).map(route => <li key={route.id}>
                            <span>{endpointName(route.sourceEndpointId)} → {endpointName(route.destinationEndpointId)}</span>
                            <button type="button" onClick={() => showRouteInMatrix(route)}>Afficher dans la matrice</button>
                            <label>Gain (dB) <input disabled={configurationLocked} type="number" min="-120" max="24" step="0.1" value={route.gainDb} onChange={event => editRoute(route.id, 'gainDb', Number(event.target.value))} /></label>
                            <label><input disabled={configurationLocked} type="checkbox" checked={route.mute} onChange={event => editRoute(route.id, 'mute', event.target.checked)} /> Muet</label>
                            <button disabled={configurationLocked} onClick={() => removeRoute(route.id)} aria-label="Supprimer la route">Supprimer</button>
                        </li>)}
                    </ul>}
                </section>

                <button className="apply-configuration" onClick={applyConfiguration} disabled={!apiState || !configuration || configurationLocked}>
                    Appliquer la configuration
                </button>
            </main>
        </div>
    );
}

export default App;
