import React, { useState } from 'react';
import EndpointStatus from './EndpointStatus';

const endpointParent = device => {
    const name = device.name || device.id;
    return /\(([^()]*)\)$/.exec(name)?.[1] || name;
};

const endpointGroupKey = device => device.containerId ? `container:${device.containerId}` : `name:${endpointParent(device)}`;
const endpointLabel = device => (device.name || device.id).replace(/\s+\([^()]*\)$/, '');

export default function WasapiDeviceTree({ devices = [], bridges = [], disabled = false, onAdd }) {
    const [showUnavailable, setShowUnavailable] = useState(false);
    const [query, setQuery] = useState('');
    const [flowFilter, setFlowFilter] = useState('all');
    const normalizedQuery = query.trim().toLocaleLowerCase();
    const visibleDevices = devices.filter(device => {
        if (!showUnavailable && device.state !== 1) return false;
        if (flowFilter !== 'all' && device.flow !== flowFilter) return false;
        if (!normalizedQuery) return true;
        return `${endpointParent(device)} ${device.name || ''} ${device.id} ${device.flow}`
            .toLocaleLowerCase().includes(normalizedQuery);
    });
    const groups = new Map();
    for (const device of visibleDevices) {
        const key = endpointGroupKey(device);
        if (!groups.has(key)) groups.set(key, { label: endpointParent(device), endpoints: [] });
        groups.get(key).endpoints.push(device);
    }
    const configuredByEndpoint = new Map();
    for (const bridge of bridges) {
        if (bridge.endpointId) {
            const configured = configuredByEndpoint.get(bridge.endpointId) || [];
            configured.push(bridge);
            configuredByEndpoint.set(bridge.endpointId, configured);
        }
    }
    const eligibleDevices = devices.filter(device => showUnavailable || device.state === 1);
    const withoutContainerId = devices.filter(device => !device.containerId).length;
    const directionFilters = [
        { id: 'all', label: `All (${eligibleDevices.length})` },
        { id: 'capture', label: `Windows inputs (${eligibleDevices.filter(device => device.flow === 'capture').length})` },
        { id: 'render', label: `Windows outputs (${eligibleDevices.filter(device => device.flow === 'render').length})` }
    ];

    return <div className="wasapi-device-tree">
        <div className="wasapi-inventory-tools">
            <label className="field">Search by device, endpoint or ID
                <input type="search" value={query} onChange={event => setQuery(event.target.value)} placeholder="e.g. Jabra, SSL, SoundWire…" />
            </label>
            <div className="wasapi-direction-filter" role="group" aria-label="Filter Windows endpoints">
                {directionFilters.map(filter => <button key={filter.id} type="button" aria-pressed={flowFilter === filter.id}
                    onClick={() => setFlowFilter(filter.id)}>{filter.label}</button>)}
            </div>
            <label className="wasapi-inactive-toggle"><input type="checkbox" checked={showUnavailable} onChange={event => setShowUnavailable(event.target.checked)} />
                Show unavailable endpoints ({devices.filter(device => device.state !== 1).length})</label>
        </div>
        {withoutContainerId > 0 && <p className="wasapi-grouping-notice" role="status">
            {withoutContainerId === devices.length
                ? 'The connected engine does not provide Windows hardware IDs yet. Devices are grouped by display name; restart the engine later to group them by physical device.'
                : `${withoutContainerId} endpoints have no Windows hardware ID and are grouped by display name.`}
        </p>}
        <p className="setup-description">Grouped by Windows hardware when Windows provides a container ID. Choose the direction that matches what you want the graph to do.</p>
        {[...groups.values()].sort((a, b) => a.label.localeCompare(b.label, undefined, { numeric: true })).map(({ label, endpoints }) => {
            const configuredCount = endpoints.filter(device => configuredByEndpoint.has(device.id)).length;
            return <details key={label} className="wasapi-device-branch" open={Boolean(normalizedQuery) || flowFilter !== 'all' || configuredCount > 0 || undefined}>
                <summary><strong>{label}</strong><span>{endpoints.filter(device => device.flow === 'capture').length} inputs · {endpoints.filter(device => device.flow === 'render').length} outputs
                    {configuredCount > 0 ? ` · ${configuredCount} configured` : ''}</span></summary>
                {['capture', 'render'].map(flow => {
                    const endpointsForFlow = endpoints.filter(device => device.flow === flow);
                    if (!endpointsForFlow.length) return null;
                    return <section key={flow} className="wasapi-flow-branch">
                        <h4>{flow === 'capture' ? 'Windows inputs → Timox graph' : 'Timox graph → Windows outputs'}</h4>
                        <ul>{endpointsForFlow.sort((a, b) => endpointLabel(a).localeCompare(endpointLabel(b), undefined, { numeric: true })).map(device => {
                            const active = device.state === 1;
                            const channels = device.mixFormat?.channels;
                            const configured = configuredByEndpoint.get(device.id) || [];
                            const sameDirectionAdded = configured.some(bridge => bridge.kind === flow);
                            const loopbackAdded = configured.some(bridge => bridge.kind === 'endpointLoopback');
                            return <li key={device.id} className="wasapi-endpoint-row" data-endpoint-id={device.id}>
                                <div className="wasapi-endpoint-summary">
                                    <div className="wasapi-endpoint-copy">
                                        <strong title={device.name || device.id}>{endpointLabel(device)}</strong>
                                        <span>{channels ? `${channels} channels` : 'Channel count unavailable'}{device.mixFormat?.sampleRate ? ` · ${device.mixFormat.sampleRate} Hz` : ''}</span>
                                        {configured.length > 0 && <span className="wasapi-configured">Added: {configured.map(bridge => `${bridge.id} (${({ capture: 'Windows input', render: 'Windows output', endpointLoopback: 'output mix copy', processLoopback: 'process copy' })[bridge.kind] || bridge.kind})`).join(', ')}</span>}
                                        {device.error && <span className="wasapi-unavailable">{device.error}</span>}
                                        <details className="wasapi-endpoint-technical"><summary>Windows endpoint ID</summary><code>{device.id}</code></details>
                                    </div>
                                    <EndpointStatus active={active} />
                                    <div className="wasapi-endpoint-actions">
                                        <button type="button" disabled={disabled || sameDirectionAdded} onClick={() => onAdd(device, flow)}>
                                            {sameDirectionAdded ? 'Already added' : active ? flow === 'capture' ? 'Add input to graph' : 'Add output from graph' : 'Save offline'}
                                        </button>
                                        {flow === 'render' && <button type="button" disabled={disabled || loopbackAdded} onClick={() => onAdd(device, 'endpointLoopback')}>
                                            {loopbackAdded ? 'Output mix copy added' : 'Copy output mix'}
                                        </button>}
                                    </div>
                                </div>
                                {channels > 0 && <details className="wasapi-channel-disclosure"><summary>Show {channels} channels</summary>
                                    <ol className="wasapi-channel-list">{Array.from({ length: channels }, (_, index) => <li key={index}>Channel {index + 1}</li>)}</ol>
                                </details>}
                            </li>;
                        })}</ul>
                    </section>;
                })}
            </details>;
        })}
        {!groups.size && <p>{normalizedQuery ? 'No devices match this search.' : `No ${showUnavailable ? '' : 'active '}Windows endpoints detected.`}</p>}
    </div>;
}
