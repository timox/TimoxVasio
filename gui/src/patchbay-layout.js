const columnGap = 160;
const rowGap = 72;
const groupGap = 112;
const compare = (a, b) => a < b ? -1 : a > b ? 1 : 0;
const positive = (value, fallback) => Number.isFinite(value) && value > 0 ? value : fallback;

// Signal chains flow left to right. Feedback cycles share a layer; independent
// graphs get separate rectangles, packed into shelves instead of one tall pile.
export const arrangePatchbayNodes = (nodes, edges = []) => {
    if (!nodes.length) return [];
    const byId = new Map(nodes.map(node => [node.id, node]));
    const ids = [...byId.keys()].sort(compare);
    const outgoing = new Map(ids.map(id => [id, new Set()]));
    const incoming = new Map(ids.map(id => [id, new Set()]));
    edges.forEach(({ source, target }) => {
        if (source !== target && byId.has(source) && byId.has(target)) {
            outgoing.get(source).add(target);
            incoming.get(target).add(source);
        }
    });
    const size = id => {
        const node = byId.get(id);
        return { width: Math.max(320, positive(node.measured?.width, 320)),
            height: positive(node.measured?.height, positive(node.data?.layoutHeight, 150)) };
    };
    // Collapse strongly connected components before assigning directed ranks.
    const index = new Map(), low = new Map(), stack = [], onStack = new Set(), sccOf = new Map(), sccs = [];
    let nextIndex = 0;
    const visit = id => {
        index.set(id, nextIndex); low.set(id, nextIndex++); stack.push(id); onStack.add(id);
        [...outgoing.get(id)].sort(compare).forEach(target => {
            if (!index.has(target)) { visit(target); low.set(id, Math.min(low.get(id), low.get(target))); }
            else if (onStack.has(target)) low.set(id, Math.min(low.get(id), index.get(target)));
        });
        if (low.get(id) === index.get(id)) {
            const members = []; let member;
            do { member = stack.pop(); onStack.delete(member); members.push(member); sccOf.set(member, sccs.length); } while (member !== id);
            sccs.push(members);
        }
    };
    ids.forEach(id => { if (!index.has(id)) visit(id); });
    const predecessors = sccs.map(() => new Set());
    outgoing.forEach((targets, source) => targets.forEach(target => {
        if (sccOf.get(source) !== sccOf.get(target)) predecessors[sccOf.get(target)].add(sccOf.get(source));
    }));
    const ranks = new Map();
    const rank = component => {
        if (!ranks.has(component)) ranks.set(component, Math.max(0, ...[...predecessors[component]].map(parent => rank(parent) + 1)));
        return ranks.get(component);
    };
    sccs.forEach((_, component) => rank(component));

    const seen = new Set(), groups = [];
    ids.forEach(root => {
        if (seen.has(root)) return;
        const members = [], pending = [root]; seen.add(root);
        while (pending.length) {
            const id = pending.pop(); members.push(id);
            [...outgoing.get(id), ...incoming.get(id)].forEach(neighbor => {
                if (!seen.has(neighbor)) { seen.add(neighbor); pending.push(neighbor); }
            });
        }
        const layers = [];
        members.sort(compare).forEach(id => {
            const level = rank(sccOf.get(id));
            if (!layers[level]) layers[level] = [];
            layers[level].push(id);
        });
        // Barycentric sweeps reduce crossings with deterministic tie-breaking.
        for (let pass = 0; pass < 4; pass++) {
            const forward = pass % 2 === 0;
            const order = layers.map((_, i) => i);
            if (!forward) order.reverse();
            order.forEach(level => {
                const positions = new Map(layers.flatMap(layer => layer.map((id, i) => [id, i])));
                const score = id => {
                    const neighbors = [...(forward ? incoming : outgoing).get(id)]
                        .filter(other => rank(sccOf.get(other)) !== level);
                    return neighbors.length ? neighbors.reduce((sum, other) => sum + positions.get(other), 0) / neighbors.length : positions.get(id);
                };
                layers[level].sort((a, b) => score(a) - score(b) || compare(a, b));
            });
        }
        // Wide fan-outs and feedback groups also need spatial distribution:
        // wrap a large rank into lanes before positioning the next rank.
        const layouts = layers.map(layer => {
            const averageWidth = layer.reduce((sum, id) => sum + size(id).width, 0) / layer.length;
            const averageHeight = layer.reduce((sum, id) => sum + size(id).height, 0) / layer.length;
            const lanes = layer.length < 4 ? 1 : Math.min(layer.length, Math.max(2, Math.ceil(Math.sqrt(layer.length * averageHeight * 1.7 / averageWidth))));
            const widths = Array(lanes).fill(0), heights = Array(Math.ceil(layer.length / lanes)).fill(0);
            layer.forEach((id, i) => {
                widths[i % lanes] = Math.max(widths[i % lanes], size(id).width);
                heights[Math.floor(i / lanes)] = Math.max(heights[Math.floor(i / lanes)], size(id).height);
            });
            const offset = (values, at) => values.slice(0, at).reduce((sum, value) => sum + value + rowGap, 0);
            return { width: offset(widths, widths.length) - rowGap, height: offset(heights, heights.length) - rowGap,
                positions: layer.map((id, i) => [id, { x: offset(widths, i % lanes),
                    y: offset(heights, Math.floor(i / lanes)) + (heights[Math.floor(i / lanes)] - size(id).height) / 2 }]) };
        });
        const height = Math.max(...layouts.map(layout => layout.height)), positions = new Map();
        let x = 0;
        layouts.forEach(layout => {
            layout.positions.forEach(([id, position]) => positions.set(id, { x: x + position.x, y: (height - layout.height) / 2 + position.y }));
            x += layout.width + columnGap;
        });
        groups.push({ key: members[0], positions, width: x - columnGap, height, count: members.length });
    });
    groups.sort((a, b) => b.count - a.count || compare(a.key, b.key));
    const area = groups.reduce((sum, group) => sum + (group.width + groupGap) * (group.height + groupGap), 0);
    const targetWidth = Math.max(...groups.map(group => group.width), Math.sqrt(area * 1.7));
    const positions = new Map();
    let x = 30, y = 30, shelfHeight = 0;
    groups.forEach(group => {
        if (x > 30 && x - 30 + group.width > targetWidth) { x = 30; y += shelfHeight + groupGap; shelfHeight = 0; }
        group.positions.forEach((position, id) => positions.set(id, { x: x + position.x, y: y + position.y }));
        x += group.width + groupGap; shelfHeight = Math.max(shelfHeight, group.height);
    });
    return nodes.map(node => ({ ...node, position: positions.get(node.id) }));
};
