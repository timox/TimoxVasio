import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import RoutingPatchbay from './RoutingPatchbay';

jest.mock('@xyflow/react', () => ({
    ReactFlow: ({ edges }) => <pre data-testid="edges">{JSON.stringify(edges)}</pre>,
    Background: () => null,
    useNodesState: nodes => [nodes, () => {}, () => {}],
    Position: { Left: 'left', Right: 'right' }
}));

test.each([[-12, true], [null, false], [undefined, false]])('cable animation uses a known source peak (%s)', async (peakDbfs, animated) => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    const route = { id: 'r1', sourceEndpointId: 'source', destinationEndpointId: 'sink', gainDb: 0, mute: false };
    try {
        await act(async () => root.render(<RoutingPatchbay
            sourceGroups={[{ id: 'app', label: 'App', kind: 'virtual-output', endpoints: [{ id: 'source', name: 'Out' }] }]}
            destinationGroups={[{ id: 'card', label: 'Card', kind: 'physical-output', endpoints: [{ id: 'sink', name: 'In' }] }]}
            routes={[route]} confirmedRoutes={[route]} activeRoutes={[route]} meters={{ source: { peakDbfs } }} />));
        const edges = JSON.parse(container.querySelector('[data-testid="edges"]').textContent);
        expect(edges).toHaveLength(1);
        expect(edges[0].animated).toBe(animated);
        if (animated) expect(edges[0].ariaLabel).toContain('estimated signal');
    } finally { await act(async () => root.unmount()); container.remove(); }
});
