const STORAGE_KEY = 'timoxvasio.route-visuals.v1';
export const DEFAULT_ROUTE_COLOR = '#2c6572';
const isColor = value => typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value);

export const routeVisualKey = route => JSON.stringify([route.sourceEndpointId, route.destinationEndpointId]);

export const readRouteVisuals = () => {
    try {
        const stored = JSON.parse(window.localStorage.getItem(STORAGE_KEY) || '{}');
        if (!stored || typeof stored !== 'object' || Array.isArray(stored)) return {};
        return Object.fromEntries(Object.entries(stored).filter(([, value]) =>
            value && typeof value === 'object' && !Array.isArray(value)).map(([key, value]) => [key, {
            label: typeof value.label === 'string' ? value.label.slice(0, 60) : '',
            color: isColor(value.color) ? value.color.toLowerCase() : DEFAULT_ROUTE_COLOR
        }]));
    } catch (_) {
        return {};
    }
};

export const saveRouteVisuals = visuals => {
    try { window.localStorage.setItem(STORAGE_KEY, JSON.stringify(visuals)); }
    catch (_) { /* Display preferences must never block routing. */ }
};
