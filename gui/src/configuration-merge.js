const sameRoute = (left, right) => !!left && !!right &&
    ['id', 'sourceEndpointId', 'destinationEndpointId', 'gainDb', 'mute']
        .every(field => left[field] === right[field]);

function mergeConfigurationDraft(base, draft, latest) {
    if (!base || !draft || !latest) throw new Error('Cannot reconcile an incomplete configuration.');
    const merged = { ...latest, routes: [...latest.routes] };
    for (const field of ['physicalDriverId', 'sampleRate', 'bufferFrames']) {
        if (draft[field] === base[field]) continue;
        if (latest[field] !== base[field] && latest[field] !== draft[field])
            throw new Error(`The ${field} setting changed in another application. Refresh before applying.`);
        merged[field] = draft[field];
    }
    const baseById = new Map(base.routes.map(route => [route.id, route]));
    const draftById = new Map(draft.routes.map(route => [route.id, route]));
    const latestById = new Map(latest.routes.map(route => [route.id, route]));
    for (const [id, original] of baseById) {
        const edited = draftById.get(id);
        if (sameRoute(original, edited)) continue;
        const current = latestById.get(id);
        if (!current) {
            if (edited) throw new Error(`Connection ${id} was removed in another application. Refresh before applying.`);
            continue;
        }
        if (!sameRoute(current, original) && !sameRoute(current, edited))
            throw new Error(`Connection ${id} changed in another application. Refresh before applying.`);
        merged.routes = merged.routes.filter(route => route.id !== id);
        if (edited) merged.routes.push(edited);
    }
    for (const [id, added] of draftById) {
        if (baseById.has(id)) continue;
        const current = latestById.get(id);
        if (current) {
            if (!sameRoute(current, added))
                throw new Error(`Connection ${id} was also created in another application. Refresh before applying.`);
            continue;
        }
        const samePair = merged.routes.find(route =>
            route.sourceEndpointId === added.sourceEndpointId &&
            route.destinationEndpointId === added.destinationEndpointId);
        if (samePair) {
            if (samePair.gainDb !== added.gainDb || samePair.mute !== added.mute)
                throw new Error('The same channel connection was changed in another application. Refresh before applying.');
            continue;
        }
        merged.routes.push(added);
    }
    return merged;
}

module.exports = { mergeConfigurationDraft };
