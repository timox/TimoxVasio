import React, { useState } from 'react';

export default function WasapiDeviceTree({ devices = [], disabled = false, onAdd }) {
    const [showUnavailable, setShowUnavailable] = useState(false);
    const [query, setQuery] = useState('');
    // Display grouping only; endpoint IDs remain distinct even for identical names.
    const groups = new Map();
    for (const device of devices) {
        if (!showUnavailable && device.state !== 1) continue;
        if (query.trim() && !(device.name || device.id).toLocaleLowerCase().includes(query.trim().toLocaleLowerCase())) continue;
        const label = /\(([^()]*)\)$/.exec(device.name || '')?.[1] || device.name || device.id;
        if (!groups.has(label)) groups.set(label, []);
        groups.get(label).push(device);
    }
    return <div className="wasapi-device-tree">
        <div className="wasapi-inventory-tools"><label className="field">Find a device<input type="search" value={query} onChange={event => setQuery(event.target.value)} placeholder="Device name…" /></label>
        <label className="wasapi-inactive-toggle"><input type="checkbox" checked={showUnavailable} onChange={event => setShowUnavailable(event.target.checked)} /> Show unavailable endpoints ({devices.filter(device => device.state !== 1).length})</label>
        </div>
        <p className="setup-description">{devices.filter(device => device.state === 1).length} active endpoints · Grouped by Windows name</p>
        {[...groups].sort(([a], [b]) => a.localeCompare(b)).map(([label, endpoints]) => <details key={label} className="wasapi-device-branch" open={query.trim() ? true : undefined}>
            <summary><strong>{label}</strong><span>{endpoints.filter(device => device.flow === 'capture').length} input endpoints · {endpoints.filter(device => device.flow === 'render').length} output endpoints</span></summary>
            {['capture', 'render'].map(flow => {
                const ports = endpoints.filter(device => device.flow === flow);
                return ports.length > 0 && <details key={flow} className="wasapi-flow-branch" open>
                    <summary>{flow === 'capture' ? 'Inputs · Windows → routing' : 'Outputs · routing → Windows'}</summary>
                    <ul>{ports.map(device => {
                        const active = device.state === 1, channels = device.mixFormat?.channels;
                        return <li key={device.id} data-endpoint-id={device.id}><details className="wasapi-endpoint-branch">
                            <summary><span className="wasapi-endpoint-name">{device.name || device.id}</span>
                                <span>{channels ? `${channels} channels` : 'Channels unavailable'}{device.mixFormat?.sampleRate ? ` · ${device.mixFormat.sampleRate} Hz` : ''}</span>
                                <span className={active ? 'wasapi-ready' : 'wasapi-unavailable'}>{active ? 'Available' : 'Unavailable · offline configuration'}</span></summary>
                            {channels > 0 ? <ol className="wasapi-channel-list">{Array.from({ length: channels }, (_, index) => <li key={index}>Channel {index + 1}</li>)}</ol>
                                : <p className="hint">Channel count will be discovered when this endpoint becomes available.</p>}
                            {device.error && <p className="hint">{device.error}</p>}
                            <div className="analysis-actions"><button type="button" disabled={disabled} onClick={() => onAdd(device, flow)}>
                                {active ? flow === 'capture' ? 'Add input' : 'Add output' : 'Add offline'}</button>
                                {flow === 'render' && <button type="button" disabled={disabled} onClick={() => onAdd(device, 'endpointLoopback')}>Capture this output{active ? '' : ' offline'}</button>}</div>
                            <p className="wasapi-endpoint-id">Endpoint ID: {device.id}</p>
                        </details></li>;
                    })}</ul>
                </details>;
            })}
        </details>)}
        {!groups.size && <p>{query.trim() ? 'No devices match this search.' : `No ${showUnavailable ? '' : 'active '}Windows endpoints detected.`}</p>}
    </div>;
}
