import { transferRoutesToDriver } from './hardware-transfer';

test('driver transfer changes only physical endpoints and keeps gain, mute and virtual links', () => {
    const routes = [
        { id: 'left', sourceEndpointId: 'virtual:TimoxVasio:app:scsynth.exe:output:1',
            destinationEndpointId: 'physical:{old}:output:1', gainDb: 3, mute: true },
        { id: 'virtual', sourceEndpointId: 'virtual:TimoxVasio:app:scsynth.exe:output:2',
            destinationEndpointId: 'virtual:TimoxVasio:app:renoise.exe:input:1', gainDb: -2, mute: false }
    ];
    const result = transferRoutesToDriver(routes, { id: '{new}', inputEndpoints: [],
        outputEndpoints: [{ id: 'physical:{new}:output:1' }] });
    expect(result.missing).toEqual([]);
    expect(result.routes).toEqual([{ ...routes[0], destinationEndpointId: 'physical:{new}:output:1' }, routes[1]]);
});

test('driver transfer refuses a missing physical port without dropping a connection', () => {
    const route = { id: 'right', sourceEndpointId: 'virtual:TimoxVasio:app:scsynth.exe:output:2',
        destinationEndpointId: 'physical:{old}:output:2', gainDb: 0, mute: false };
    const result = transferRoutesToDriver([route], { id: '{new}', inputEndpoints: [], outputEndpoints: [] });
    expect(result.routes).toEqual([]);
    expect(result.missing).toEqual(['output 2']);
    expect(route.destinationEndpointId).toBe('physical:{old}:output:2');
});
