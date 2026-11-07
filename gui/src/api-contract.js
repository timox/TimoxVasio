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

function deviceTopologyKey(state) {
    return JSON.stringify({ physicalDrivers: state.physicalDrivers || [], stereoPairs: state.stereoPairs || [],
        virtualDrivers: (state.virtualDrivers || []).map(driver => ({ ...driver,
            clients: (driver.clients || []).map(({ activity, processCpuPercent, processCpuWindowMs, ...identity }) => identity)
        })) });
}

function mergeClientObservations(state, payload) {
    if (!state || payload?.owner !== 'engine' || !Array.isArray(payload.clients)) return state;
    return { ...state, virtualDrivers: (state.virtualDrivers || []).map(driver => ({ ...driver,
        clients: (driver.clients || []).map(client => {
            const observation = payload.clients.find(item => item.driverId === driver.id && item.processId === client.pid &&
                (item.processCreationTime ?? null) === (client.processCreationTime ?? null));
            return observation ? { ...client, activity: observation.activity,
                processCpuPercent: observation.processCpuPercent, processCpuWindowMs: observation.processCpuWindowMs } : client;
        })
    })) };
}

module.exports = { configurationFromState, applyCommand, endpointLabel, deviceTopologyKey, mergeClientObservations };
