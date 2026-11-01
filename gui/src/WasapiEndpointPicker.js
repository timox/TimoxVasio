import React from 'react';
import EndpointStatus from './EndpointStatus';

const parentName = device => {
    const name = device.name || device.id;
    return /\(([^()]*)\)$/.exec(name)?.[1] || name;
};

const endpointName = device => (device.name || device.id).replace(/\s+\([^()]*\)$/, '');

export default function WasapiEndpointPicker({ devices = [], selectableFlow, selectedId = '', disabled = false, onSelect }) {
    const groups = new Map();
    for (const device of devices) {
        const key = device.containerId ? `container:${device.containerId}` : `name:${parentName(device)}`;
        if (!groups.has(key)) groups.set(key, { label: parentName(device), devices: [] });
        groups.get(key).devices.push(device);
    }

    return <fieldset className="wasapi-endpoint-picker" aria-label="Windows device endpoints" disabled={disabled}>
        <legend className="sr-only">Select a Windows audio endpoint</legend>
        {selectedId && !devices.some(device => device.id === selectedId) && <label className="wasapi-picker-endpoint wasapi-picker-orphan">
            <input type="radio" name="wasapi-endpoint" value={selectedId} checked onChange={() => onSelect(selectedId)} />
            <span><strong>Saved endpoint not detected</strong><small>{selectedId}</small></span>
            <EndpointStatus active={false} />
        </label>}
        {[...groups.entries()].sort((a, b) => a[1].label.localeCompare(b[1].label, undefined, { numeric: true })).map(([key, group]) =>
            <details className="wasapi-picker-device" key={key} open={group.devices.some(device => device.id === selectedId) || groups.size === 1 || undefined}>
                <summary><strong>{group.label}</strong><span>
                    {group.devices.filter(device => device.flow === 'capture').length} input · {group.devices.filter(device => device.flow === 'render').length} output
                </span></summary>
                <ul>{group.devices.sort((a, b) => endpointName(a).localeCompare(endpointName(b), undefined, { numeric: true })).map(device => {
                    const channels = device.mixFormat?.channels;
                    const sampleRate = device.mixFormat?.sampleRate;
                    const selectable = device.flow === selectableFlow;
                    const detail = <><span className={`wasapi-picker-direction ${device.flow === 'capture' ? 'input' : 'output'}`}>{device.flow === 'capture' ? 'INPUT' : 'OUTPUT'}</span>
                        <span className="wasapi-picker-copy"><strong>{endpointName(device)}</strong><small>{[
                            channels ? `${channels} channels` : null,
                            sampleRate ? `${sampleRate} Hz` : null
                        ].filter(Boolean).join(' · ') || 'Format unavailable'}</small>
                            {device.error && <small className="wasapi-unavailable">{device.error}</small>}</span>
                        <EndpointStatus active={device.state === 1} /></>;
                    return <li key={device.id}>{selectable ? <label className="wasapi-picker-endpoint">
                        <input type="radio" name="wasapi-endpoint" value={device.id} checked={selectedId === device.id}
                            onChange={() => onSelect(device.id)} />{detail}
                    </label> : <div className="wasapi-picker-endpoint wasapi-picker-unselectable" aria-disabled="true">
                        <span className="wasapi-picker-indent" aria-hidden="true" />{detail}<small>Choose the other device operation to select this endpoint.</small>
                    </div>}</li>;
                })}</ul>
            </details>
        )}
        {!groups.size && !selectedId && <p>No matching Windows endpoints found.</p>}
    </fieldset>;
}
