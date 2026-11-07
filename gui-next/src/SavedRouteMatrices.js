import React from 'react';
import { DEFAULT_ROUTE_COLOR, routeVisualKey } from './route-visuals';

const pairKey = (source, destination) => `${source}\u0000${destination}`;

function RouteConnection({ route, channelLabel, endpointName, confirmed, activeIds, routeVisuals,
    selectedRouteId, onHighlight, onShowMatrix, onEdit, onRemove, disabled }) {
    const saved = confirmed.get(pairKey(route.sourceEndpointId, route.destinationEndpointId));
    const applied = saved && saved.gainDb === route.gainDb && saved.mute === route.mute;
    const status = !applied ? 'Pending change' : activeIds.has(route.id) ? 'Active' : 'Waiting for channels';
    const visual = routeVisuals[routeVisualKey(route)];
    return <details className={`saved-route-connection${selectedRouteId === route.id ? ' route-highlighted' : ''}`}
        style={{ '--route-color': visual?.color || DEFAULT_ROUTE_COLOR }}>
        <summary aria-label={`${endpointName(route.sourceEndpointId)} to ${endpointName(route.destinationEndpointId)}: ${status}`}>
            <span className="saved-route-link" aria-hidden="true">↗</span>
            <span>{channelLabel || status}</span>
            {channelLabel && <span className="saved-route-cell-status">{status}</span>}
        </summary>
        <div className="saved-route-connection-detail">
            {visual?.label && <strong className="route-custom-label">{visual.label}</strong>}
            <p>{endpointName(route.sourceEndpointId)} → {endpointName(route.destinationEndpointId)}</p>
            <div className="saved-route-actions">
                <button type="button" className="highlight-route" aria-pressed={selectedRouteId === route.id}
                    onClick={() => onHighlight(route)}>{selectedRouteId === route.id ? 'Clear highlight' : 'Highlight connection'}</button>
                <button type="button" onClick={() => onShowMatrix(route)}>Show in matrix</button>
                <label>Gain (dB) <input disabled={disabled} type="number" min="-120" max="24" step="0.1"
                    value={route.gainDb} onChange={event => onEdit(route.id, 'gainDb', Number(event.target.value))} /></label>
                <label><input disabled={disabled} type="checkbox" checked={route.mute}
                    onChange={event => onEdit(route.id, 'mute', event.target.checked)} /> Muted</label>
                <button type="button" disabled={disabled} onClick={() => onRemove(route.id)}
                    aria-label={`Remove route ${endpointName(route.sourceEndpointId)} to ${endpointName(route.destinationEndpointId)}`}>Remove</button>
            </div>
        </div>
    </details>;
}

export default function SavedRouteMatrices({ groups, endpointName, confirmedRoutes, activeRoutes, routeVisuals,
    selectedRouteId, onHighlight, onShowMatrix, onEdit, onRemove, disabled }) {
    const confirmed = new Map(confirmedRoutes.map(route => [pairKey(route.sourceEndpointId, route.destinationEndpointId), route]));
    const activeIds = new Set(activeRoutes.map(route => route.id));
    const connection = (route, channelLabel = '') => <RouteConnection key={route.id} route={route}
        channelLabel={channelLabel} endpointName={endpointName} confirmed={confirmed} activeIds={activeIds}
        routeVisuals={routeVisuals} selectedRouteId={selectedRouteId} onHighlight={onHighlight}
        onShowMatrix={onShowMatrix} onEdit={onEdit} onRemove={onRemove} disabled={disabled} />;

    return <div className="saved-route-groups">
        {groups.map(group => {
            const entries = group.routes.map((route, index) => ({ route, ...group.channels[index] }));
            const sources = [...new Set(entries.map(entry => entry.source))].sort((a, b) => a - b);
            const destinations = [...new Set(entries.map(entry => entry.destination))].sort((a, b) => a - b);
            const showMatrix = sources.every(Number.isFinite) && destinations.every(Number.isFinite) &&
                sources.length * destinations.length <= 256;
            const cells = new Map();
            if (showMatrix) for (const entry of entries) {
                const key = pairKey(entry.source, entry.destination);
                cells.set(key, [...(cells.get(key) || []), entry.route]);
            }
            return <details className="saved-route-group" key={group.key}>
                <summary><span className="saved-route-group-name">{group.sourceLabel} → {group.destinationLabel}</span>
                    <span className="saved-route-group-channels">{group.channelSummary} · {group.routes.length} {group.routes.length === 1 ? 'route' : 'routes'}</span>
                    <span className={`saved-route-group-status${group.applied ? ' applied' : ' pending'}`}>
                        {group.applied ? `Applied · ${group.activeCount}/${group.routes.length} active` : 'Pending changes'}</span></summary>
                {showMatrix ? <div className="saved-route-matrix-scroll"><table className="saved-route-matrix">
                    <caption className="sr-only">{group.sourceLabel} to {group.destinationLabel}</caption>
                    <thead><tr><th scope="col">Source ↓ / destination →</th>
                        {destinations.map(channel => <th key={channel} scope="col">Channel {channel}</th>)}</tr></thead>
                    <tbody>{sources.map(source => <tr key={source}><th scope="row">Channel {source}</th>
                        {destinations.map(destination => <td key={destination} className={cells.has(pairKey(source, destination)) ? 'connected' : 'empty'}>
                            {(cells.get(pairKey(source, destination)) || []).map(route => connection(route))}
                        </td>)}</tr>)}</tbody>
                </table></div> : <div className="saved-route-dense-list">
                    <p className="hint">Many channels in this pair; showing connected channels only.</p>
                    {entries.map(entry => connection(entry.route,
                        Number.isFinite(entry.source) && Number.isFinite(entry.destination)
                            ? `${entry.source} → ${entry.destination}` : 'Connection'))}
                </div>}
            </details>;
        })}
    </div>;
}
