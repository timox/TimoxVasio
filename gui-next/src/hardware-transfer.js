export const HARDWARE_TRANSFER_KEY = 'timoxvasio.hardware-transfer.v1';

export const readHardwareTransfer = () => {
    try {
        const plan = JSON.parse(window.localStorage.getItem(HARDWARE_TRANSFER_KEY) || 'null');
        return plan && typeof plan.targetDriverId === 'string' && Array.isArray(plan.routes) ? plan : null;
    } catch (_) { return null; }
};

export const saveHardwareTransfer = plan => {
    try {
        if (plan) window.localStorage.setItem(HARDWARE_TRANSFER_KEY, JSON.stringify(plan));
        else window.localStorage.removeItem(HARDWARE_TRANSFER_KEY);
    } catch (_) { /* A storage failure must not change the audio configuration. */ }
};

const physicalPort = endpointId => {
    const match = /^physical:(.+):(input|output):(\d+)$/.exec(endpointId);
    return match ? { direction: match[2], channel: Number(match[3]) } : null;
};

export const transferRoutesToDriver = (routes, driver) => {
    const available = new Set([...(driver?.inputEndpoints || []), ...(driver?.outputEndpoints || [])]
        .map(endpoint => endpoint.id));
    const missing = new Set();
    const mapped = routes.map(route => {
        const next = { ...route };
        for (const field of ['sourceEndpointId', 'destinationEndpointId']) {
            const port = physicalPort(route[field]);
            if (!port) continue;
            const target = `physical:${driver?.id}:${port.direction}:${port.channel}`;
            if (available.has(target)) next[field] = target;
            else missing.add(`${port.direction} ${port.channel}`);
        }
        return next;
    });
    return { routes: missing.size ? [] : mapped, missing: [...missing] };
};
