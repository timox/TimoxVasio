function configurationFromState(state) {
    if (!state || state.apiVersion !== '1.0') throw new Error('Version de l’API VASIO invalide');
    return {
        physicalDriverId: state.engine.physicalDriverId,
        sampleRate: state.engine.sampleRate,
        bufferFrames: state.engine.bufferFrames,
        routes: state.routes.map(({ id, sourceEndpointId, destinationEndpointId, gainDb, mute }) =>
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
