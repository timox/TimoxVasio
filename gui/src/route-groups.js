const endpointGroups = groups => new Map(groups.flatMap(group =>
    group.endpoints.map(endpoint => [endpoint.id, { group, channel: endpoint.channel }])));

const range = (first, last) => first === last ? String(first) : `${first}–${last}`;

const summarizeChannels = entries => {
    const sorted = [...entries].sort((a, b) => a.source - b.source || a.destination - b.destination);
    const runs = [];
    for (const entry of sorted) {
        const current = runs[runs.length - 1];
        if (current && entry.source === current.sourceEnd + 1 &&
            entry.destination === current.destinationEnd + 1) {
            current.sourceEnd = entry.source;
            current.destinationEnd = entry.destination;
        } else runs.push({ sourceStart: entry.source, sourceEnd: entry.source,
            destinationStart: entry.destination, destinationEnd: entry.destination });
    }
    if (runs.some(run => !Number.isFinite(run.sourceStart) || !Number.isFinite(run.destinationStart)))
        return `${entries.length} channel connections`;
    const shown = runs.slice(0, 3).map(run =>
        `${range(run.sourceStart, run.sourceEnd)} → ${range(run.destinationStart, run.destinationEnd)}`);
    return `${shown.join(', ')}${runs.length > 3 ? `, +${runs.length - 3} more` : ''}`;
};

export function groupRoutes(routes, sourceGroups, destinationGroups, confirmedRoutes) {
    const sources = endpointGroups(sourceGroups);
    const destinations = endpointGroups(destinationGroups);
    const confirmed = new Map(confirmedRoutes.map(route =>
        [`${route.sourceEndpointId}\u0000${route.destinationEndpointId}`, route]));
    const groups = new Map();
    for (const route of routes) {
        const source = sources.get(route.sourceEndpointId);
        const destination = destinations.get(route.destinationEndpointId);
        const sourceId = source?.group.id || route.sourceEndpointId;
        const destinationId = destination?.group.id || route.destinationEndpointId;
        const key = `${sourceId}\u0000${destinationId}`;
        if (!groups.has(key)) groups.set(key, { key, sourceLabel: source?.group.label || sourceId,
            destinationLabel: destination?.group.label || destinationId, routes: [], channels: [], applied: true });
        const group = groups.get(key);
        group.routes.push(route);
        group.channels.push({ source: source?.channel, destination: destination?.channel });
        const saved = confirmed.get(`${route.sourceEndpointId}\u0000${route.destinationEndpointId}`);
        if (!saved || saved.gainDb !== route.gainDb || saved.mute !== route.mute) group.applied = false;
    }
    return [...groups.values()].map(group => ({ ...group,
        channelSummary: summarizeChannels(group.channels) }));
}
