const endpointPrefix = name => `virtual:timoxvasio:app:${name.trim().toLocaleLowerCase()}:`;

export function transferProcessConnections(routes, sourceName, destinationName) {
    const sourcePrefix = endpointPrefix(sourceName);
    const destinationPrefix = endpointPrefix(destinationName);
    if (!sourceName.trim() || !destinationName.trim() || sourcePrefix === destinationPrefix)
        return { routes, moved: 0, duplicates: 0 };

    const remap = id => id.toLocaleLowerCase().startsWith(sourcePrefix)
        ? `virtual:TimoxVasio:app:${destinationName.trim().toLocaleLowerCase()}:` + id.slice(sourcePrefix.length) : id;
    const belongs = route => [route.sourceEndpointId, route.destinationEndpointId]
        .some(id => id.toLocaleLowerCase().startsWith(sourcePrefix));
    const existing = routes.filter(route => !belongs(route));
    const routeKey = route => `${route.sourceEndpointId}|${route.destinationEndpointId}`.toLocaleLowerCase();
    const keys = new Set(existing.map(routeKey));
    let moved = 0;
    let duplicates = 0;
    const transferred = routes.filter(belongs).map(route => {
        moved++;
        return { ...route, sourceEndpointId: remap(route.sourceEndpointId),
            destinationEndpointId: remap(route.destinationEndpointId) };
    }).filter(route => {
        const key = routeKey(route);
        if (keys.has(key)) { duplicates++; return false; }
        keys.add(key);
        return true;
    });
    return { routes: [...existing, ...transferred], moved, duplicates };
}
