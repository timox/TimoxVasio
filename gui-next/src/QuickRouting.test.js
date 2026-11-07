import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import QuickRouting from './QuickRouting';

test('quick routing keeps channel numbers aligned when earlier source channels are already routed', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const channels = [1, 2, 3, 4];
    const sourceGroups = [{ id: 'app', label: 'Renoise', kind: 'virtual-output',
        endpoints: channels.map(channel => ({ id: `app:${channel}`, channel })) }];
    const destinationGroups = [{ id: 'card', label: 'SSL', kind: 'physical-output',
        endpoints: channels.map(channel => ({ id: `card:${channel}`, channel })) }];
    const routes = [1, 2].map(channel => ({ sourceEndpointId: `app:${channel}`,
        destinationEndpointId: `other:${channel}` }));
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<QuickRouting sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={routes} onAddRoutes={() => {}} />));
        expect(container.querySelector('.quick-routing-preview').textContent).toContain('Renoise 3–4 → SSL 3–4');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});
