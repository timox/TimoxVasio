import React, { useState } from 'react';

const compatible = (source, destination) => source === 'physical-input'
    ? destination === 'virtual-input'
    : source === 'virtual-output' && ['virtual-input', 'physical-output'].includes(destination);

const startsFor = (group, stereo) => {
    const endpoints = group?.endpoints || [];
    if (!stereo) return endpoints;
    const channels = new Set(endpoints.map(endpoint => endpoint.channel));
    return endpoints.filter(endpoint => channels.has(endpoint.channel + 1));
};

const pair = (group, start, stereo) => !start ? [] : stereo
    ? [start, group.endpoints.find(endpoint => endpoint.channel === start.channel + 1)].filter(Boolean)
    : [start];

const pickStart = (starts, selected, used, group, stereo, preferredChannel) => starts.find(endpoint => endpoint.id === selected)
    || starts.find(endpoint => endpoint.channel === preferredChannel &&
        pair(group, endpoint, stereo).every(channel => !used.has(channel.id)))
    || starts.find(endpoint => pair(group, endpoint, stereo).every(channel => !used.has(channel.id)))
    || starts[0];

export default function QuickRouting({ sourceGroups = [], destinationGroups = [], routes = [], onAddRoutes,
    disabled = false }) {
    const [sourceGroupId, setSourceGroupId] = useState('');
    const [destinationGroupId, setDestinationGroupId] = useState('');
    const [sourceStartId, setSourceStartId] = useState('');
    const [destinationStartId, setDestinationStartId] = useState('');
    const [stereo, setStereo] = useState(true);
    const sourceGroup = sourceGroups.find(group => group.id === sourceGroupId) || sourceGroups[0];
    const destinations = destinationGroups.filter(group => sourceGroup && compatible(sourceGroup.kind, group.kind));
    const destinationGroup = destinations.find(group => group.id === destinationGroupId) || destinations[0];
    const usedSources = new Set(routes.map(route => route.sourceEndpointId));
    const usedDestinations = new Set(routes.map(route => route.destinationEndpointId));
    const sourceStarts = startsFor(sourceGroup, stereo);
    const destinationStarts = startsFor(destinationGroup, stereo);
    const sourceStart = pickStart(sourceStarts, sourceStartId, usedSources, sourceGroup, stereo);
    const destinationStart = pickStart(destinationStarts, destinationStartId, usedDestinations,
        destinationGroup, stereo, sourceStart?.channel);
    const sources = pair(sourceGroup, sourceStart, stereo);
    const targets = pair(destinationGroup, destinationStart, stereo);
    const proposed = sources.map((source, index) => ({ sourceEndpointId: source.id,
        destinationEndpointId: targets[index]?.id })).filter(route => route.destinationEndpointId);
    const missing = proposed.filter(route => !routes.some(existing =>
        existing.sourceEndpointId === route.sourceEndpointId &&
        existing.destinationEndpointId === route.destinationEndpointId));
    const channelLabel = endpoints => endpoints.map(endpoint => endpoint.channel).join('–');
    const add = () => {
        if (!missing.length || disabled) return;
        onAddRoutes(missing);
        setSourceStartId('');
        setDestinationStartId('');
    };

    return <div className="quick-routing">
        <p className="hint">Choose where the sound should go. This adds routes to the draft; Apply configuration activates them.</p>
        <div className="quick-routing-fields">
            <label className="field">Source
                <select aria-label="Quick route source" value={sourceGroup?.id || ''}
                    onChange={event => { setSourceGroupId(event.target.value); setDestinationGroupId(''); setSourceStartId(''); setDestinationStartId(''); }}>
                    {sourceGroups.map(group => <option key={group.id} value={group.id}>{group.label}</option>)}
                </select>
            </label>
            <label className="field">Destination
                <select aria-label="Quick route destination" value={destinationGroup?.id || ''}
                    onChange={event => { setDestinationGroupId(event.target.value); setDestinationStartId(''); }}>
                    {destinations.map(group => <option key={group.id} value={group.id}>{group.label}</option>)}
                </select>
            </label>
        </div>
        <div className="quick-routing-mode" role="group" aria-label="Route width">
            <button type="button" className={stereo ? 'selected' : ''} aria-pressed={stereo} onClick={() => { setStereo(true); setSourceStartId(''); setDestinationStartId(''); }}>Stereo</button>
            <button type="button" className={!stereo ? 'selected' : ''} aria-pressed={!stereo} onClick={() => { setStereo(false); setSourceStartId(''); setDestinationStartId(''); }}>Mono</button>
        </div>
        <div className="quick-routing-fields">
            <label className="field">Source {stereo ? 'pair' : 'channel'}
                <select aria-label="Quick route source channels" value={sourceStart?.id || ''}
                    onChange={event => setSourceStartId(event.target.value)}>
                    {sourceStarts.map(start => <option key={start.id} value={start.id}>{channelLabel(pair(sourceGroup, start, stereo))}</option>)}
                </select>
            </label>
            <label className="field">Destination {stereo ? 'pair' : 'channel'}
                <select aria-label="Quick route destination channels" value={destinationStart?.id || ''}
                    onChange={event => setDestinationStartId(event.target.value)}>
                    {destinationStarts.map(start => <option key={start.id} value={start.id}>{channelLabel(pair(destinationGroup, start, stereo))}</option>)}
                </select>
            </label>
        </div>
        {proposed.length ? <p className="quick-routing-preview" role="status">
            Proposal: {sourceGroup.label} {channelLabel(sources)} → {destinationGroup.label} {channelLabel(targets)}
            {missing.length < proposed.length ? ' · Existing connections will be kept.' : ''}
        </p> : <p className="hint">No matching channels are available in this selection.</p>}
        <p className="hint">Channel availability means no route is recorded; routes do not lock channels.</p>
        <button type="button" className="quick-routing-add" disabled={disabled || !missing.length} onClick={add}>
            Add {missing.length === 1 ? 'route' : 'routes'} to draft
        </button>
    </div>;
}
