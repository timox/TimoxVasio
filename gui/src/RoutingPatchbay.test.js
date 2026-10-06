import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import RoutingPatchbay from './RoutingPatchbay';

const sourceGroups = [
    { id: 'app', label: 'AudioApp.exe', kind: 'virtual-output', endpoints: [
        { id: 'virtual:TimoxVasio:42:output:1', name: 'Out 1' }
    ] },
    { id: 'hardware-in', label: 'Interface input', kind: 'physical-input', endpoints: [
        { id: 'physical:device:input:1', name: 'Input 1' }
    ] }
];
const destinationGroups = [
    { id: 'hardware-out', label: 'Interface output', kind: 'physical-output', endpoints: [
        { id: 'physical:device:output:1', name: 'Output 1' }
    ] },
    { id: 'app-in', label: 'AudioApp.exe', kind: 'virtual-input', endpoints: [
        { id: 'virtual:TimoxVasio:42:input:1', name: 'In 1' }
    ] }
];

test('a selected source connects to a compatible destination and existing links are visible', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const onToggleRoute = jest.fn();
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[]} onToggleRoute={onToggleRoute} />));
        expect(container.textContent).toContain('AudioApp.exe');
        expect(container.querySelectorAll('.patchbay-port')).toHaveLength(4);
        await act(async () => Simulate.click(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')));
        await act(async () => Simulate.click(container.querySelector('[data-port-id="physical:device:output:1"]')));
        expect(onToggleRoute).toHaveBeenCalledWith('virtual:TimoxVasio:42:output:1', 'physical:device:output:1');
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[{ id: 'route-1',
                sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
                destinationEndpointId: 'physical:device:output:1' }]} onToggleRoute={onToggleRoute} />));
        expect(container.querySelector('[data-port-id="physical:device:output:1"]').getAttribute('aria-label')).toContain('connected');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('a physical input cannot connect to a physical output', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const onToggleRoute = jest.fn();
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[]} onToggleRoute={onToggleRoute} />));
        await act(async () => Simulate.click(container.querySelector('[data-port-id="physical:device:input:1"]')));
        expect(container.querySelector('[data-port-id="physical:device:output:1"]').disabled).toBe(true);
        await act(async () => Simulate.click(container.querySelector('[data-port-id="virtual:TimoxVasio:42:input:1"]')));
        expect(onToggleRoute).toHaveBeenCalledWith('physical:device:input:1', 'virtual:TimoxVasio:42:input:1');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('dragging from a source port to a destination creates one draft connection', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const onToggleRoute = jest.fn();
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[]} onToggleRoute={onToggleRoute} />));
        const source = container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]');
        const destination = container.querySelector('[data-port-id="physical:device:output:1"]');
        await act(async () => {
            Simulate.pointerDown(source);
            Simulate.pointerMove(container.querySelector('.patchbay-scroll'), { buttons: 1, clientX: 120, clientY: 80 });
        });
        await act(async () => Simulate.pointerUp(destination));
        expect(onToggleRoute).toHaveBeenCalledTimes(1);
        expect(onToggleRoute).toHaveBeenCalledWith(source.dataset.portId, destination.dataset.portId);
        await act(async () => new Promise(resolve => setTimeout(resolve, 0)));
        await act(async () => Simulate.click(source));
        await act(async () => Simulate.click(destination));
        expect(onToggleRoute).toHaveBeenCalledTimes(2);
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('selecting a cable highlights its endpoints without editing the route', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const onToggleRoute = jest.fn();
    const onSelectRoute = jest.fn();
    const onEditRoute = jest.fn();
    const routes = [{ id: 'route-1', sourceEndpointId: 'virtual:TimoxVasio:42:output:1',
        destinationEndpointId: 'physical:device:output:1', gainDb: 0, mute: false }];
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={routes}
            selectedRouteId={null} onSelectRoute={onSelectRoute} onToggleRoute={onToggleRoute}
            onEditRoute={onEditRoute} />));
        await act(async () => Simulate.click(container.querySelector('[data-route-id="route-1"]')));
        expect(onSelectRoute).toHaveBeenCalledWith('route-1');
        expect(onToggleRoute).not.toHaveBeenCalled();
        await act(async () => Simulate.contextMenu(container.querySelector('[data-route-id="route-1"]')));
        expect(onSelectRoute).toHaveBeenCalledWith('route-1');
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={routes}
            selectedRouteId="route-1" onSelectRoute={onSelectRoute} onToggleRoute={onToggleRoute}
            onEditRoute={onEditRoute} />));
        expect(container.querySelector('.patchbay-inspector')).not.toBeNull();
        expect(container.querySelector('.patchbay-cables path.connection.selected')).not.toBeNull();
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]').classList.contains('route-selected')).toBe(true);
        expect(container.querySelector('[data-port-id="physical:device:output:1"]').classList.contains('route-selected')).toBe(true);
        expect(container.textContent).toContain('Selected connection: Out 1 → Output 1');
        await act(async () => Simulate.change(container.querySelector('.route-gain-input'), { target: { value: '-6' } }));
        await act(async () => Simulate.change(container.querySelector('.route-mute-input'), { target: { checked: true } }));
        expect(onEditRoute).toHaveBeenCalledWith('route-1', 'gainDb', -6);
        expect(onEditRoute).toHaveBeenCalledWith('route-1', 'mute', true);
        await act(async () => Simulate.click(container.querySelector('.remove-selected-connection')));
        expect(onToggleRoute).toHaveBeenCalledWith('virtual:TimoxVasio:42:output:1', 'physical:device:output:1');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('large port groups collapse and a selected connection can reveal them', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const manySources = [{ ...sourceGroups[0], endpoints: [sourceGroups[0].endpoints[0],
        ...Array.from({ length: 9 }, (_, index) => ({ id: `virtual:extra:${index}`, name: `Out ${index + 2}` }))] }];
    const routes = [{ id: 'route-1', sourceEndpointId: sourceGroups[0].endpoints[0].id,
        destinationEndpointId: destinationGroups[0].endpoints[0].id }];
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={manySources}
            destinationGroups={destinationGroups} routes={routes} selectedRouteId="route-1" onToggleRoute={jest.fn()} />));
        const groupButton = container.querySelector('[data-group-key="source:app"]');
        expect(groupButton.getAttribute('aria-expanded')).toBe('false');
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).toBeNull();
        expect(container.textContent).toContain('hidden by collapsed groups or filters');
        await act(async () => Simulate.click(container.querySelector('.reveal-selected-connection')));
        expect(container.querySelector('[data-group-key="source:app"]').getAttribute('aria-expanded')).toBe('true');
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).not.toBeNull();
        await act(async () => Simulate.click(container.querySelector('[data-group-key="source:app"]')));
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).toBeNull();
        await act(async () => Simulate.change(container.querySelector('.patchbay-search input'), { target: { value: 'Out 1' } }));
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).not.toBeNull();
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});
