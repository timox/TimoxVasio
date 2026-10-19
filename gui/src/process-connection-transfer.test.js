import { transferProcessConnections } from './process-connection-transfer';

test('moving saved process connections preserves unrelated routes and target settings', () => {
    const routes = [
        { id: 'old-1', sourceEndpointId: 'virtual:TimoxVasio:app:traktor4.exe:output:1',
            destinationEndpointId: 'virtual:TimoxVasio:app:scsynth.exe:input:1', gainDb: 3, mute: true },
        { id: 'old-2', sourceEndpointId: 'virtual:TimoxVasio:app:traktor4.exe:output:2',
            destinationEndpointId: 'virtual:TimoxVasio:app:scsynth.exe:input:2', gainDb: 0, mute: false },
        { id: 'current', sourceEndpointId: 'virtual:timoxvasio:app:traktor pro 4.exe:output:1',
            destinationEndpointId: 'virtual:TimoxVasio:app:scsynth.exe:input:1', gainDb: -6, mute: false },
        { id: 'hardware', sourceEndpointId: 'virtual:TimoxVasio:app:scsynth.exe:output:1',
            destinationEndpointId: 'physical:device:output:1', gainDb: 0, mute: false }
    ];
    const result = transferProcessConnections(routes, 'traktor4.exe', 'Traktor Pro 4.exe');
    expect(result.moved).toBe(2);
    expect(result.duplicates).toBe(1);
    expect(result.routes).toHaveLength(3);
    expect(result.routes.find(route => route.id === 'current').gainDb).toBe(-6);
    expect(result.routes.find(route => route.id === 'old-2').sourceEndpointId)
        .toBe('virtual:TimoxVasio:app:traktor pro 4.exe:output:2');
    expect(result.routes.find(route => route.id === 'hardware')).toBe(routes[3]);
    expect(routes[0].sourceEndpointId).toContain('traktor4.exe');
});
