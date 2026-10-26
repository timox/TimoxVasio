import { arrangePatchbayNodes } from './patchbay-layout';

test('measured blocks do not overlap and arranging is repeatable', () => {
    const nodes = [
        { id: 'input', data: { layoutColumn: 0 }, measured: { width: 500, height: 100 } },
        { id: 'app-a', data: { layoutColumn: 1 }, measured: { width: 400, height: 640 } },
        { id: 'app-b', data: { layoutColumn: 1, layoutHeight: 90 } },
        { id: 'output', data: { layoutColumn: 2 } }
    ].map(node => ({ ...node, position: { x: -100, y: -100 } }));
    const arranged = arrangePatchbayNodes(nodes);
    expectNoOverlap(arranged);
    expect(arrangePatchbayNodes(arranged)).toEqual(arranged);
    expect(nodes[0].position).toEqual({ x: -100, y: -100 });
    expect(arrangePatchbayNodes([])).toEqual([]);
});

const block = (id, column = 1, width = 320, height = 180) => ({ id, data: { layoutColumn: column },
    measured: { width, height }, position: { x: -100, y: -100 } });
const cable = (source, target) => ({ id: `${source}-${target}`, source, target });
function expectNoOverlap(nodes) {
    for (let i = 0; i < nodes.length; i++) for (let j = i + 1; j < nodes.length; j++) {
        const a = nodes[i], b = nodes[j];
        const width = node => node.measured?.width || 320;
        const height = node => node.measured?.height || node.data.layoutHeight || 150;
        expect(a.position.x + width(a) + 35 <= b.position.x || b.position.x + width(b) + 35 <= a.position.x ||
            a.position.y + height(a) + 35 <= b.position.y || b.position.y + height(b) + 35 <= a.position.y).toBe(true);
    }
}
test('connected applications follow signal direction instead of stacking in one column', () => {
    const nodes = [block('out', 2), block('app-b'), block('in', 0), block('app-a')];
    const edges = [cable('in', 'app-a'), cable('app-a', 'app-b'), cable('app-b', 'out')];
    const arranged = arrangePatchbayNodes(nodes, edges);
    const x = id => arranged.find(node => node.id === id).position.x;
    expect(x('in')).toBeLessThan(x('app-a'));
    expect(x('app-a')).toBeLessThan(x('app-b'));
    expect(x('app-b')).toBeLessThan(x('out'));
    expectNoOverlap(arranged);
    expect(arrangePatchbayNodes(arranged, edges)).toEqual(arranged);
});
test('isolated blocks occupy several rows and columns rather than a tall pile', () => {
    const arranged = arrangePatchbayNodes(Array.from({ length: 12 }, (_, i) => block(`app-${i}`)));
    expect(new Set(arranged.map(node => node.position.x)).size).toBeGreaterThan(1);
    expect(new Set(arranged.map(node => node.position.y)).size).toBeGreaterThan(1);
    const width = Math.max(...arranged.map(node => node.position.x + 320)) - 30;
    const height = Math.max(...arranged.map(node => node.position.y + 180)) - 30;
    expect(height / width).toBeLessThan(1.5);
    expectNoOverlap(arranged);
});
test('cycles, branching, disconnected chains and measured tall nodes stay separated', () => {
    const nodes = [block('a', 1, 510, 850), block('b'), block('c'), block('d'), block('e', 1, 420, 700), block('f')];
    const edges = [cable('a', 'b'), cable('b', 'a'), cable('b', 'c'), cable('a', 'd'), cable('e', 'f'), cable('missing', 'a')];
    const arranged = arrangePatchbayNodes(nodes, edges);
    expectNoOverlap(arranged);
    expect(arrangePatchbayNodes(arranged, edges)).toEqual(arranged);
    expect(arranged.find(node => node.id === 'c').position.x).toBeGreaterThan(arranged.find(node => node.id === 'b').position.x);
    expect(nodes.every(node => node.position.x === -100)).toBe(true);
});
test('a large fan-out and a feedback group spread across several lanes', () => {
    const nodes = [block('source', 0), ...Array.from({ length: 12 }, (_, i) => block(`target-${i}`))];
    const edges = nodes.slice(1).map(node => cable('source', node.id));
    for (const connections of [edges, [...edges, ...nodes.slice(1).map(node => cable(node.id, 'source'))]]) {
        const arranged = arrangePatchbayNodes(nodes, connections);
        const targets = arranged.filter(node => node.id !== 'source');
        expect(new Set(targets.map(node => node.position.x)).size).toBeGreaterThan(1);
        const width = Math.max(...arranged.map(node => node.position.x + 320)) - 30;
        const height = Math.max(...arranged.map(node => node.position.y + 180)) - 30;
        expect(height / width).toBeLessThan(1.5);
        expectNoOverlap(arranged);
    }
});
