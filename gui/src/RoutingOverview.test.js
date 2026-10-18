import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import RoutingOverview from './RoutingOverview';

test('overview counts saved application routes while the client is connected', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingOverview virtualClients={[{ pid: 42,
            processName: 'Renoise.exe' }]} routes={[{ sourceEndpointId:
            'virtual:TimoxVasio:app:renoise.exe:output:1', destinationEndpointId: 'physical:card:output:1' }]} />));
        expect(container.querySelector('.application-flow').textContent).toContain('1 routed output');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});
