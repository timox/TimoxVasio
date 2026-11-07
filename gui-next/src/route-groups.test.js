import { groupRoutes } from './route-groups';

test('disconnected application routes still form one readable equipment pair', () => {
    const routes = [1, 2].map(channel => ({ id: `route-${channel}`,
        sourceEndpointId: `virtual:TimoxVasio:app:mixxx.exe:output:${channel}`,
        destinationEndpointId: `physical:card-1:output:${channel}`,
        gainDb: 0, mute: false }));
    const groups = groupRoutes(routes, [], [], routes);
    expect(groups).toHaveLength(1);
    expect(groups[0].sourceLabel).toBe('mixxx.exe · Outputs');
    expect(groups[0].destinationLabel).toBe('Physical outputs');
    expect(groups[0].channelSummary).toBe('1–2 → 1–2');
});
