import React, { useCallback, useLayoutEffect, useRef, useState } from 'react';
import { DEFAULT_ROUTE_COLOR, routeVisualKey } from './route-visuals';

const permitted = (sourceKind, destinationKind) =>
    sourceKind === 'physical-input' ? destinationKind === 'virtual-input' :
        sourceKind === 'virtual-output' && ['physical-output', 'virtual-input'].includes(destinationKind);

const withSearch = (groups, search) => {
    const query = search.trim().toLocaleLowerCase();
    return groups.map(group => ({ ...group, endpoints: group.endpoints.filter(endpoint =>
        !query || `${group.label} ${endpoint.name} ${endpoint.channel || ''}`.toLocaleLowerCase().includes(query)) }))
        .filter(group => group.endpoints.length);
};

const curve = (start, end) => {
    const bend = Math.max(40, (end.x - start.x) * 0.45);
    return `M ${start.x} ${start.y} C ${start.x + bend} ${start.y}, ${end.x - bend} ${end.y}, ${end.x} ${end.y}`;
};

export default function RoutingPatchbay({ sourceGroups = [], destinationGroups = [], routes = [], onToggleRoute,
    selectedRouteId = null, onSelectRoute = () => {}, routeVisuals = {}, onChangeRouteVisual = () => {},
    onBulkChangeRouteVisuals = () => {}, onEditRoute = () => {}, disabled = false }) {
    const [selectedSourceId, setSelectedSourceId] = useState(null);
    const [sourceSearch, setSourceSearch] = useState('');
    const [destinationSearch, setDestinationSearch] = useState('');
    const [collapsedGroups, setCollapsedGroups] = useState({});
    const [bulkOpen, setBulkOpen] = useState(false);
    const [bulkSearch, setBulkSearch] = useState('');
    const [bulkSelectedKeys, setBulkSelectedKeys] = useState([]);
    const [bulkLabel, setBulkLabel] = useState('');
    const [bulkColor, setBulkColor] = useState(DEFAULT_ROUTE_COLOR);
    const [bulkUseLabel, setBulkUseLabel] = useState(false);
    const [bulkUseColor, setBulkUseColor] = useState(false);
    const [bulkNotice, setBulkNotice] = useState('');
    const [points, setPoints] = useState({});
    const [dragPoint, setDragPoint] = useState(null);
    const stageRef = useRef(null);
    const ignoreClickRef = useRef(false);
    const selectedSourceRef = useRef(null);
    const selectSource = id => { selectedSourceRef.current = id; setSelectedSourceId(id); };
    const sources = withSearch(sourceGroups, sourceSearch);
    const destinations = withSearch(destinationGroups, destinationSearch);
    const groupKey = (side, group) => `${side}:${group.id}`;
    const isCollapsed = (group, side) => {
        const search = side === 'source' ? sourceSearch : destinationSearch;
        return !search.trim() && (collapsedGroups[groupKey(side, group)] ?? group.endpoints.length > 8);
    };
    const visibleSourceIds = new Set(sources.filter(group => !isCollapsed(group, 'source'))
        .flatMap(group => group.endpoints.map(endpoint => endpoint.id)));
    const visibleDestinationIds = new Set(destinations.filter(group => !isCollapsed(group, 'destination'))
        .flatMap(group => group.endpoints.map(endpoint => endpoint.id)));
    const sourceMap = new Map(sourceGroups.flatMap(group => group.endpoints.map(endpoint => [endpoint.id, group.kind])));
    const selectedKind = sourceMap.get(selectedSourceId);
    const linkedDestinations = new Set(routes.map(route => route.destinationEndpointId));
    const selectedRoute = routes.find(route => route.id === selectedRouteId);
    const selectedVisual = selectedRoute && routeVisuals[routeVisualKey(selectedRoute)];
    const endpointNames = new Map([...sourceGroups, ...destinationGroups]
        .flatMap(group => group.endpoints.map(endpoint => [endpoint.id, endpoint.name])));
    const describeRoute = route => `${routeVisuals[routeVisualKey(route)]?.label ? `${routeVisuals[routeVisualKey(route)].label} · ` : ''}${endpointNames.get(route.sourceEndpointId) || route.sourceEndpointId} → ${endpointNames.get(route.destinationEndpointId) || route.destinationEndpointId}`;
    const bulkQuery = bulkSearch.trim().toLocaleLowerCase();
    const bulkVisibleRoutes = routes.filter(route => !bulkQuery || describeRoute(route).toLocaleLowerCase().includes(bulkQuery));
    const bulkSelectedRoutes = routes.filter(route => bulkSelectedKeys.includes(routeVisualKey(route)));
    const toggleBulkRoute = (key, selected) => setBulkSelectedKeys(current => selected
        ? [...new Set([...current, key])] : current.filter(item => item !== key));
    const routeVisible = route => visibleSourceIds.has(route.sourceEndpointId) && visibleDestinationIds.has(route.destinationEndpointId);
    const selectedRouteVisible = !!(selectedRoute && routeVisible(selectedRoute));
    const hiddenRouteCount = routes.filter(route => !routeVisible(route)).length;

    const setAllGroupsCollapsed = collapsed => {
        setCollapsedGroups(Object.fromEntries([
            ...sourceGroups.map(group => [groupKey('source', group), collapsed]),
            ...destinationGroups.map(group => [groupKey('destination', group), collapsed])
        ]));
        if (collapsed) { selectSource(null); setDragPoint(null); }
    };

    const revealSelectedRoute = () => {
        if (!selectedRoute) return;
        setSourceSearch('');
        setDestinationSearch('');
        const sourceGroup = sourceGroups.find(group => group.endpoints.some(endpoint => endpoint.id === selectedRoute.sourceEndpointId));
        const destinationGroup = destinationGroups.find(group => group.endpoints.some(endpoint => endpoint.id === selectedRoute.destinationEndpointId));
        setCollapsedGroups(current => ({ ...current,
            ...(sourceGroup ? { [groupKey('source', sourceGroup)]: false } : {}),
            ...(destinationGroup ? { [groupKey('destination', destinationGroup)]: false } : {})
        }));
    };

    const measure = useCallback(() => {
        const stage = stageRef.current;
        if (!stage) return;
        const bounds = stage.getBoundingClientRect();
        const next = {};
        stage.querySelectorAll('[data-port-id]').forEach(button => {
            const rect = button.getBoundingClientRect();
            next[button.dataset.portId] = {
                x: button.dataset.side === 'source' ? rect.right - bounds.left : rect.left - bounds.left,
                y: rect.top + rect.height / 2 - bounds.top
            };
        });
        setPoints(next);
    }, []);

    useLayoutEffect(() => {
        measure();
        const stage = stageRef.current;
        if (typeof ResizeObserver !== 'undefined' && stage) {
            const observer = new ResizeObserver(measure);
            observer.observe(stage);
            return () => observer.disconnect();
        }
        window.addEventListener('resize', measure);
        return () => window.removeEventListener('resize', measure);
    }, [measure, sourceGroups, destinationGroups, sourceSearch, destinationSearch, collapsedGroups, routes]);

    const connect = (destinationId, destinationKind) => {
        const sourceId = selectedSourceRef.current;
        if (disabled || !sourceId || !permitted(sourceMap.get(sourceId), destinationKind)) return;
        onToggleRoute(sourceId, destinationId);
        selectSource(null);
        setDragPoint(null);
    };

    const renderGroup = (group, side) => {
        const collapsed = isCollapsed(group, side);
        const groupEndpointIds = new Set(group.endpoints.map(endpoint => endpoint.id));
        const linkCount = routes.filter(route => groupEndpointIds.has(side === 'source' ? route.sourceEndpointId : route.destinationEndpointId)).length;
        return <section className="patchbay-interface" key={group.id}>
        <div className="patchbay-interface-title"><button type="button" className="patchbay-group-toggle"
            data-group-key={groupKey(side, group)} aria-expanded={!collapsed}
            disabled={!!(side === 'source' ? sourceSearch : destinationSearch).trim()}
            onClick={() => {
                setCollapsedGroups(current => ({ ...current, [groupKey(side, group)]: !collapsed }));
                if (!collapsed && side === 'source' && groupEndpointIds.has(selectedSourceId)) {
                    selectSource(null); setDragPoint(null);
                }
            }}>
            <span className="patchbay-disclosure" aria-hidden="true">{collapsed ? '▸' : '▾'}</span>
            <strong>{group.label}</strong>
            <span>{group.endpoints.length} ports{linkCount ? ` · ${linkCount} ${linkCount === 1 ? 'link' : 'links'}` : ''}</span>
        </button></div>
        {!collapsed && <ul>{group.endpoints.map(endpoint => {
            const connected = side === 'destination' && linkedDestinations.has(endpoint.id);
            const invalid = side === 'destination' && selectedSourceId && !permitted(selectedKind, group.kind);
            return <li key={endpoint.id}>
                <button type="button" className={`patchbay-port ${side}${selectedSourceId === endpoint.id ? ' selected' : ''}${connected ? ' linked' : ''}${selectedRoute && (side === 'source' ? selectedRoute.sourceEndpointId : selectedRoute.destinationEndpointId) === endpoint.id ? ' route-selected' : ''}`}
                    data-port-id={endpoint.id} data-side={side} disabled={disabled || !!invalid}
                    aria-pressed={side === 'source' ? selectedSourceId === endpoint.id : undefined}
                    aria-label={side === 'source' ? `${group.label}, ${endpoint.name}, select as source` :
                        `${group.label}, ${endpoint.name}${connected ? ', connected' : ''}${selectedSourceId ? ', select as destination' : ''}`}
                    title={endpoint.name}
                    onClick={() => {
                        if (ignoreClickRef.current) { ignoreClickRef.current = false; return; }
                        if (side === 'source') selectSource(endpoint.id);
                        else connect(endpoint.id, group.kind);
                    }}
                    onPointerDown={side === 'source' ? () => { if (!disabled) selectSource(endpoint.id); } : undefined}
                    onPointerUp={side === 'destination' ? () => {
                        if (dragPoint && !invalid) {
                            ignoreClickRef.current = true;
                            window.setTimeout(() => { ignoreClickRef.current = false; }, 0);
                            connect(endpoint.id, group.kind);
                        }
                    } : undefined}>
                    {side === 'destination' && <i className="patchbay-socket" aria-hidden="true" />}
                    <span>{endpoint.name}</span>
                    {side === 'source' && <i className="patchbay-socket" aria-hidden="true" />}
                </button>
            </li>;
        })}</ul>}
    </section>;
    };

    return <div className="patchbay" style={{ '--selected-route-color': selectedVisual?.color || DEFAULT_ROUTE_COLOR }}>
        <div className="patchbay-toolbar">
            <p>Select an output and then an input, or drag a cable between two ports. Click or right-click a cable to edit it in the inspector. Changes remain pending until you select “Apply configuration”.</p>
            <div className="patchbay-toolbar-actions">
                <button type="button" onClick={() => setAllGroupsCollapsed(false)}>Expand all</button>
                <button type="button" onClick={() => setAllGroupsCollapsed(true)}>Collapse all</button>
                <button type="button" onClick={() => { selectSource(null); setDragPoint(null); }} disabled={!selectedSourceId}>Cancel selection</button>
            </div>
        </div>
        <div className="patchbay-search">
            <label>Search sources<input value={sourceSearch} onChange={event => setSourceSearch(event.target.value)} placeholder="Interface or channel" /></label>
            <label>Search destinations<input value={destinationSearch} onChange={event => setDestinationSearch(event.target.value)} placeholder="Interface or channel" /></label>
        </div>
        <div className="patchbay-workspace">
        <div className="patchbay-scroll" role="region" aria-label="Graphical connections" tabIndex="0"
            onPointerMove={event => {
                if (!selectedSourceRef.current || !event.buttons || !stageRef.current) return;
                const bounds = stageRef.current.getBoundingClientRect();
                setDragPoint({ x: event.clientX - bounds.left, y: event.clientY - bounds.top });
            }}
            onPointerLeave={() => setDragPoint(null)} onPointerCancel={() => setDragPoint(null)}
            onKeyDown={event => { if (event.key === 'Escape') { selectSource(null); setDragPoint(null); } }}>
            <div className="patchbay-stage" ref={stageRef}>
                <svg className="patchbay-cables" aria-hidden="true" width="100%" height="100%">
                    {[...routes].sort((a, b) => Number(a.id === selectedRouteId) - Number(b.id === selectedRouteId))
                        .map(route => routeVisible(route) && points[route.sourceEndpointId] && points[route.destinationEndpointId]
                        ? <g key={route.id} style={{ '--route-color': routeVisuals[routeVisualKey(route)]?.color || DEFAULT_ROUTE_COLOR }}>
                            <title>{`${routeVisuals[routeVisualKey(route)]?.label ? `${routeVisuals[routeVisualKey(route)].label}: ` : ''}${endpointNames.get(route.sourceEndpointId) || route.sourceEndpointId} → ${endpointNames.get(route.destinationEndpointId) || route.destinationEndpointId}`}</title>
                            <path d={curve(points[route.sourceEndpointId], points[route.destinationEndpointId])}
                                className={`connection${route.mute ? ' muted' : ''}${route.id === selectedRouteId ? ' selected' : selectedRouteId ? ' subdued' : ''}`} />
                            <path data-route-id={route.id}
                                d={curve(points[route.sourceEndpointId], points[route.destinationEndpointId])}
                                className="connection-hit" onClick={() => onSelectRoute(route.id)}
                                onContextMenu={event => { event.preventDefault(); onSelectRoute(route.id); }} />
                        </g> : null)}
                    {selectedSourceId && dragPoint && points[selectedSourceId] &&
                        <path d={curve(points[selectedSourceId], dragPoint)} className="preview" />}
                </svg>
                <div className="patchbay-column"><h3>Sources</h3>
                    {sources.length ? sources.map(group => renderGroup(group, 'source')) : <p className="patchbay-empty">No visible sources.</p>}
                </div>
                <div className="patchbay-column"><h3>Destinations</h3>
                    {destinations.length ? destinations.map(group => renderGroup(group, 'destination')) : <p className="patchbay-empty">No visible destinations.</p>}
                </div>
            </div>
        </div>
        <aside className="patchbay-inspector" aria-label="Connection properties">
            <h3>Connection properties</h3>
            <button type="button" className="bulk-edit-toggle" aria-expanded={bulkOpen}
                onClick={() => { setBulkOpen(current => !current); setBulkNotice(''); }}>
                {bulkOpen ? 'Edit one connection' : 'Edit multiple connections'}
            </button>
            {bulkOpen ? <div className="bulk-route-editor">
                <label className="bulk-route-search">Find connections
                    <input value={bulkSearch} onChange={event => setBulkSearch(event.target.value)} placeholder="Label or channel" />
                </label>
                <div className="bulk-route-list" role="group" aria-label="Connections to customize">
                    {bulkVisibleRoutes.length ? bulkVisibleRoutes.map(route => <label key={routeVisualKey(route)}>
                        <input className="bulk-route-checkbox" type="checkbox"
                            checked={bulkSelectedKeys.includes(routeVisualKey(route))}
                            onChange={event => { toggleBulkRoute(routeVisualKey(route), event.target.checked); setBulkNotice(''); }} />
                        <span>{describeRoute(route)}</span>
                    </label>) : <p>No matching connections.</p>}
                </div>
                <div className="bulk-route-actions">
                    <button type="button" onClick={() => setBulkSelectedKeys(current =>
                        [...new Set([...current, ...bulkVisibleRoutes.map(routeVisualKey)])])} disabled={!bulkVisibleRoutes.length}>Select shown</button>
                    <button type="button" onClick={() => setBulkSelectedKeys([])} disabled={!bulkSelectedRoutes.length}>Clear selection</button>
                </div>
                <p className="bulk-route-count">{bulkSelectedRoutes.length} {bulkSelectedRoutes.length === 1 ? 'connection' : 'connections'} selected</p>
                <label className="bulk-property-toggle"><input type="checkbox" checked={bulkUseLabel}
                    onChange={event => setBulkUseLabel(event.target.checked)} /> Apply label</label>
                <input className="bulk-label-input" maxLength="60" value={bulkLabel} placeholder="e.g. Headphone"
                    onChange={event => { setBulkLabel(event.target.value); setBulkUseLabel(true); setBulkNotice(''); }} />
                <label className="bulk-property-toggle"><input type="checkbox" checked={bulkUseColor}
                    onChange={event => setBulkUseColor(event.target.checked)} /> Apply color</label>
                <input className="bulk-color-input" type="color" value={bulkColor}
                    onChange={event => { setBulkColor(event.target.value); setBulkUseColor(true); setBulkNotice(''); }} />
                <button type="button" className="bulk-apply"
                    disabled={!bulkSelectedRoutes.length || (!bulkUseLabel && !bulkUseColor)}
                    onClick={() => {
                        onBulkChangeRouteVisuals(bulkSelectedRoutes, {
                            ...(bulkUseLabel ? { label: bulkLabel.slice(0, 60) } : {}),
                            ...(bulkUseColor ? { color: bulkColor } : {})
                        });
                        setBulkNotice(`Appearance updated for ${bulkSelectedRoutes.length} ${bulkSelectedRoutes.length === 1 ? 'connection' : 'connections'}.`);
                    }}>Apply appearance to selected</button>
                <p className="route-visual-note">Appearance only · audio routes are unchanged.</p>
                {bulkNotice && <p className="bulk-notice" role="status">{bulkNotice}</p>}
            </div> : <>
            <label className="inspector-route-picker">Connection
                <select value={selectedRoute?.id || ''} onChange={event => onSelectRoute(event.target.value || null)}>
                    <option value="">Choose a connection</option>
                    {routes.map(route => <option key={route.id} value={route.id}>{describeRoute(route)}</option>)}
                </select>
            </label>
            <p className="patchbay-status" aria-live="polite">{selectedSourceId
                ? 'Source selected. Choose a compatible destination; press Escape to cancel.'
                : selectedRoute
                    ? `Selected connection: ${selectedVisual?.label ? `${selectedVisual.label} · ` : ''}${endpointNames.get(selectedRoute.sourceEndpointId) || selectedRoute.sourceEndpointId} → ${endpointNames.get(selectedRoute.destinationEndpointId) || selectedRoute.destinationEndpointId}${selectedRouteVisible ? '.' : ' (hidden by collapsed groups or filters).'}`
                    : `${routes.length} configured ${routes.length === 1 ? 'connection' : 'connections'}${hiddenRouteCount ? `; ${hiddenRouteCount} hidden by collapsed groups or filters` : ''}.`}</p>
            {selectedRoute ? <div className="patchbay-selection-actions">
            <div className="route-visual-editor">
                <label>Connection label
                    <input className="route-label-input" maxLength="60" value={selectedVisual?.label || ''}
                        placeholder="e.g. Headphone" onChange={event => onChangeRouteVisual(selectedRoute, 'label', event.target.value)} />
                </label>
                <label>Connection color
                    <input className="route-color-input" type="color" value={selectedVisual?.color || DEFAULT_ROUTE_COLOR}
                        onChange={event => onChangeRouteVisual(selectedRoute, 'color', event.target.value)} />
                </label>
                <span className="route-visual-note">Appearance only · saved locally for this source and destination.</span>
            </div>
            <div className="route-audio-editor">
                <label>Gain (dB)
                    <input className="route-gain-input" type="number" min="-120" max="24" step="0.1"
                        disabled={disabled} value={selectedRoute.gainDb ?? 0}
                        onChange={event => onEditRoute(selectedRoute.id, 'gainDb', Number(event.target.value))} />
                </label>
                <label className="route-mute-label"><input className="route-mute-input" type="checkbox"
                    disabled={disabled} checked={!!selectedRoute.mute}
                    onChange={event => onEditRoute(selectedRoute.id, 'mute', event.target.checked)} /> Mute</label>
            </div>
            {!selectedRouteVisible && <button type="button" className="reveal-selected-connection" onClick={revealSelectedRoute}>
                Show selected connection
            </button>}
            <button type="button" className="remove-selected-connection" disabled={disabled}
                onClick={() => {
                    onToggleRoute(selectedRoute.sourceEndpointId, selectedRoute.destinationEndpointId);
                    onSelectRoute(null);
                }}>Remove selected connection</button>
            </div> : <p className="patchbay-inspector-empty">Select a cable or choose a connection above to edit it.</p>}
            </>}
        </aside>
        </div>
    </div>;
}
