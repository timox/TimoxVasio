import React, { useState } from 'react';

function Availability({ title, className, endpoints, routedIds }) {
    const [expanded, setExpanded] = useState(false);
    const visible = expanded ? endpoints : endpoints.slice(0, 16);
    const available = endpoints.filter(endpoint => !routedIds.has(endpoint.id)).length;
    return <div className={`physical-availability ${className}`}>
        <div className="physical-availability-head"><strong>{title}</strong>
            <span>{available} available / {endpoints.length}</span></div>
        {endpoints.length ? <div className="physical-channel-list">
            {visible.map(endpoint => <span key={endpoint.id}
                className={`physical-channel${routedIds.has(endpoint.id) ? ' used' : ''}`}
                title={endpoint.name}>{endpoint.channel} · {routedIds.has(endpoint.id) ? 'Used in routes' : 'Available'}</span>)}
        </div> : <p className="hint">No channels published by the selected driver.</p>}
        {endpoints.length > 16 && <button type="button" onClick={() => setExpanded(current => !current)}>
            {expanded ? 'Show fewer channels' : `Show all ${endpoints.length} channels`}
        </button>}
    </div>;
}

const clientEndpoints = (routes, client, direction) => {
    const pidPrefix = `virtual:TimoxVasio:${client.pid}:${direction}:`;
    const appPrefix = `virtual:timoxvasio:app:${client.processName.toLowerCase()}:${direction}:`;
    const key = direction === 'output' ? 'sourceEndpointId' : 'destinationEndpointId';
    return new Set(routes.map(route => route[key]).filter(id =>
        id.startsWith(pidPrefix) || id.toLowerCase().startsWith(appPrefix)));
};

export default function RoutingOverview({ physicalDriver, virtualClients = [], routes = [], routeGroups = [] }) {
    const usedOutputs = new Set(routes.map(route => route.destinationEndpointId));
    const usedInputs = new Set(routes.map(route => route.sourceEndpointId));
    return <div className="routing-overview">
        <div className="routing-overview-card">
            <h3>Physical interface · {physicalDriver?.name || 'No driver selected'}</h3>
            <p className="hint">Available means no saved route uses the channel. Routes do not lock channels.</p>
            <Availability title="Outputs" className="physical-output-availability"
                endpoints={physicalDriver?.outputEndpoints || []} routedIds={usedOutputs} />
            <Availability title="Inputs" className="physical-input-availability"
                endpoints={physicalDriver?.inputEndpoints || []} routedIds={usedInputs} />
        </div>
        <div className="routing-overview-card">
            <h3>Application flows</h3>
            {virtualClients.length ? <div className="application-flow-list">
                {virtualClients.map(client => {
                    const outputs = clientEndpoints(routes, client, 'output').size;
                    const inputs = clientEndpoints(routes, client, 'input').size;
                    return <div className="application-flow" key={client.pid}>
                        <strong>{client.processName}</strong><span>{outputs} routed {outputs === 1 ? 'output' : 'outputs'} · {inputs} routed {inputs === 1 ? 'input' : 'inputs'}</span>
                    </div>;
                })}
            </div> : <p className="hint">No ASIO applications connected.</p>}
            {routeGroups.length ? <div className="flow-summaries">
                {routeGroups.map(group => <div key={group.key} className="flow-summary">
                    <strong>{group.sourceLabel} → {group.destinationLabel}</strong>
                    <span>{group.channelSummary} · {group.routes.length} {group.routes.length === 1 ? 'route' : 'routes'}</span>
                </div>)}
            </div> : <p className="hint">No saved connections yet.</p>}
        </div>
    </div>;
}
