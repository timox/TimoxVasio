import React, { useState } from 'react';

export default function WasapiDeviceTree({ devices = [], disabled = false, onAdd }) {
    const [showUnavailable, setShowUnavailable] = useState(false);
    // Display grouping only; endpoint IDs remain distinct even for identical names.
    const groups = new Map();
    for (const device of devices) {
        if (!showUnavailable && device.state !== 1) continue;
        const label = /\(([^()]*)\)$/.exec(device.name || '')?.[1] || device.name || device.id;
        if (!groups.has(label)) groups.set(label, []);
        groups.get(label).push(device);
    }
    return <div className="wasapi-device-tree">
        <p>{devices.filter(device => device.state === 1).length} active Windows endpoints. Expand a device to see inputs, outputs and channels.</p>
        <label className="wasapi-inactive-toggle"><input type="checkbox" checked={showUnavailable} onChange={event => setShowUnavailable(event.target.checked)} /> Show unavailable endpoints ({devices.filter(device => device.state !== 1).length})</label>
        <p className="hint">Grouped by Windows display name. Each endpoint stays separate. Offline endpoints can be saved with audio disabled.</p>
        {[...groups].sort(([a], [b]) => a.localeCompare(b)).map(([label, endpoints]) => <details key={label} className="wasapi-device-branch" open>
            <summary><strong>{label}</strong><span>{endpoints.filter(device => device.flow === 'capture').length} inputs · {endpoints.filter(device => device.flow === 'render').length} outputs</span></summary>
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
        {!groups.size && <p>No {showUnavailable ? '' : 'active '}Windows endpoints detected.</p>}
    </div>;
}
