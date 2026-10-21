function configurationFromState(state) {
    if (!state || !['1.0', '1.1'].includes(state.apiVersion)) throw new Error('Version de l’API VASIO invalide');
    return {
        ...(state.apiVersion === '1.1' && Array.isArray(state.configuredWasapiBridges)
            ? { wasapiBridges: state.configuredWasapiBridges.map(bridge => ({ ...bridge })) } : {}),
        ...(state.engine.clockSource ? { clockSource: { kind: state.engine.clockSource.kind } } : {}),
        physicalDriverId: state.engine.physicalDriverId,
        sampleRate: state.engine.sampleRate,
        bufferFrames: state.engine.bufferFrames,
        routes: (state.configuredRoutes || state.routes)
            .map(({ id, sourceEndpointId, destinationEndpointId, gainDb, mute }) =>
            ({ id, sourceEndpointId, destinationEndpointId, gainDb, mute }))
    };
}

function applyCommand(payload, id) {
    return { id, command: 'configuration.apply', payload };
}

function endpointLabel(endpoint) {
    return `${endpoint.name} (${endpoint.id})`;
}

module.exports = { configurationFromState, applyCommand, endpointLabel };
