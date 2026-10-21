import { arrangePatchbayNodes } from './patchbay-layout';

test('measured blocks do not overlap and arranging is repeatable', () => {
    const nodes = [
        { id: 'input', data: { layoutColumn: 0 }, measured: { width: 500, height: 100 } },
        { id: 'app-a', data: { layoutColumn: 1 }, measured: { width: 400, height: 640 } },
        { id: 'app-b', data: { layoutColumn: 1, layoutHeight: 90 } },
        { id: 'output', data: { layoutColumn: 2 } }
    ].map(node => ({ ...node, position: { x: -100, y: -100 } }));
    const arranged = arrangePatchbayNodes(nodes);
    expect(arranged[1].position.x).toBeGreaterThan(arranged[0].position.x + 500);
    expect(arranged[2].position.y).toBeGreaterThan(arranged[1].position.y + 640);
    expect(arranged[3].position.x).toBeGreaterThan(arranged[1].position.x + 400);
    expect(arrangePatchbayNodes(arranged)).toEqual(arranged);
    expect(nodes[0].position).toEqual({ x: -100, y: -100 });
    expect(arrangePatchbayNodes([])).toEqual([]);
});
