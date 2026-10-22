const sameRoute = (left, right) => !!left && !!right &&
    ['id', 'sourceEndpointId', 'destinationEndpointId', 'gainDb', 'mute']
        .every(field => left[field] === right[field]);

function mergeConfigurationDraft(base, draft, latest) {
    if (!base || !draft || !latest) throw new Error('Cannot reconcile an incomplete configuration.');
    const merged = { ...latest, routes: [...latest.routes] };
    if (Array.isArray(latest.wasapiBridges) && Array.isArray(draft.wasapiBridges)) {
        const normalize = item => item && ({ ...item, enabled: item.enabled ?? true,
            bufferDuration100ns: item.bufferDuration100ns ?? 0 });
        const same = (a, b) => !!a && !!b && ['id', 'enabled', 'kind', 'endpointId', 'defaultRole', 'bufferDuration100ns']
            .every(key => normalize(a)[key] === normalize(b)[key]);
        const original = new Map((base.wasapiBridges || []).map(item => [item.id, item]));
        const edited = new Map(draft.wasapiBridges.map(item => [item.id, item]));
        const current = new Map(latest.wasapiBridges.map(item => [item.id, item]));
        merged.wasapiBridges = [...latest.wasapiBridges];
        for (const id of new Set([...original.keys(), ...edited.keys()])) {
            const before = original.get(id), after = edited.get(id), now = current.get(id);
            if (same(before, after)) continue;
            if (now && !same(now, before) && !same(now, after))
                throw new Error(`Windows audio bridge ${id} changed in another application. Refresh before applying.`);
            if (before && after && !now)
                throw new Error(`Windows audio bridge ${id} was removed in another application. Refresh before applying.`);
            merged.wasapiBridges = merged.wasapiBridges.filter(item => item.id !== id);
            if (after) merged.wasapiBridges.push({ ...after });
        }
    }
    const clockKind = config => config.clockSource?.kind || 'asio';
    if (clockKind(draft) !== clockKind(base)) {
        if (clockKind(latest) !== clockKind(base) && clockKind(latest) !== clockKind(draft))
            throw new Error('The clock source changed in another application. Refresh before applying.');
        merged.clockSource = { kind: clockKind(draft) };
    }
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
