import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import RoutingPatchbay from './RoutingPatchbay';

const chooseContextAction = async (target, label) => {
    await act(async () => Simulate.contextMenu(target, { clientX: 100, clientY: 100 }));
    const action = [...document.querySelectorAll('.patchbay-context-menu [role="menuitem"]')]
        .find(button => button.textContent === label);
    expect(action).toBeDefined();
    await act(async () => Simulate.click(action));
};

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

test('a process context menu opens creation for that process', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const routes = [{ id: 'first', sourceEndpointId: sourceGroups[0].endpoints[0].id,
        destinationEndpointId: destinationGroups[0].endpoints[0].id },
    { id: 'second', sourceEndpointId: sourceGroups[0].endpoints[0].id,
        destinationEndpointId: destinationGroups[1].endpoints[0].id }];
    const onCreateConnection = jest.fn();
    const onToggleRoute = jest.fn();
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={routes}
            onCreateConnection={onCreateConnection} onToggleRoute={onToggleRoute} />));
        await act(async () => Simulate.contextMenu(container.querySelector('[data-node-key="process:app"]'),
            { clientX: 100, clientY: 100 }));
        expect(document.querySelector('[role="menu"]').getAttribute('aria-label')).toBe('Process actions');
        await act(async () => Simulate.click([...document.querySelectorAll('[role="menuitem"]')]
            .find(button => button.textContent === 'Add a connection…')));
        expect(onCreateConnection).toHaveBeenCalledWith('AudioApp.exe');
        await act(async () => Simulate.contextMenu(container.querySelector('[data-node-key="process:app"]'),
            { clientX: 100, clientY: 100 }));
        await act(async () => Simulate.click([...document.querySelectorAll('[role="menuitem"]')]
            .find(button => button.textContent === 'Remove a connection…')));
        expect(document.querySelectorAll('.process-dialog-routes > div')).toHaveLength(2);
        await act(async () => Simulate.click(document.querySelectorAll('.process-dialog-routes button')[3]));
        expect(onToggleRoute).toHaveBeenCalledWith(routes[1].sourceEndpointId, routes[1].destinationEndpointId);
        expect(onToggleRoute).toHaveBeenCalledTimes(1);
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('channel matrix opens in a dialog and closes with Escape', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const route = { id: 'route-1', sourceEndpointId: sourceGroups[0].endpoints[0].id,
        destinationEndpointId: destinationGroups[0].endpoints[0].id };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[route]} selectedRouteId={route.id} />));
        expect(document.querySelector('[role="dialog"]')).toBeNull();
        await chooseContextAction(container.querySelector('[data-node-key="process:app"]'), 'Modify a connection…');
        await act(async () => Simulate.click(document.querySelector('.process-dialog-routes button')));
        await act(async () => Simulate.click(document.querySelector('.inspect-connection-matrix')));
        const dialog = document.querySelector('.connection-matrix');
        expect(dialog.getAttribute('aria-modal')).toBe('true');
        expect(dialog.textContent).toContain('AudioApp.exe');
        expect(dialog.textContent).toContain('Interface output');
        await act(async () => document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })));
        expect(document.querySelector('.connection-matrix')).toBeNull();
        expect(document.activeElement).toBe(document.querySelector('.inspect-connection-matrix'));
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('saved connections can be staged for transfer to an open process', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const oldPort = { id: 'virtual:TimoxVasio:app:traktor4.exe:output:2', name: 'traktor4.exe Out 2 (saved)' };
    const currentPort = { id: 'virtual:TimoxVasio:42:output:2', name: 'Traktor Pro 4.exe Out 2' };
    const sources = [
        { id: 'TimoxVasio:42:output', label: 'Traktor Pro 4.exe · PID 42',
            kind: 'virtual-output', endpoints: [currentPort] },
        { id: 'saved:traktor4.exe:output', label: 'traktor4.exe · Saved connections (process not open)',
            kind: 'virtual-output', endpoints: [oldPort] }
    ];
    const routes = [{ id: 'old', sourceEndpointId: oldPort.id,
        destinationEndpointId: destinationGroups[0].endpoints[0].id }];
    const onTransferProcessConnections = jest.fn();
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sources}
            destinationGroups={destinationGroups} routes={routes}
            onTransferProcessConnections={onTransferProcessConnections} />));
        expect(container.querySelector('.patchbay-interface.saved')).not.toBeNull();
        await chooseContextAction(container.querySelector('.patchbay-interface.saved'), 'Configure process…');
        const picker = document.querySelector('.process-connection-transfer select');
        expect(picker.textContent).toContain('Traktor Pro 4.exe');
        await act(async () => Simulate.change(picker, { target: { value: 'Traktor Pro 4.exe' } }));
        await act(async () => Simulate.click(document.querySelector('.process-connection-transfer button')));
        expect(onTransferProcessConnections).toHaveBeenCalledWith('traktor4.exe', 'Traktor Pro 4.exe');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('a selected source connects to a compatible destination and existing links are visible', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const onToggleRoute = jest.fn();
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[{ id: 'one', sourceEndpointId: sourceGroups[0].endpoints[0].id, destinationEndpointId: destinationGroups[0].endpoints[0].id }, { id: 'two', sourceEndpointId: sourceGroups[1].endpoints[0].id, destinationEndpointId: destinationGroups[1].endpoints[0].id }]} onToggleRoute={onToggleRoute} />));
        expect(container.querySelectorAll('.patchbay-port')).toHaveLength(4);
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
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
            destinationGroups={destinationGroups} routes={[{ id: 'one', sourceEndpointId: sourceGroups[0].endpoints[0].id, destinationEndpointId: destinationGroups[0].endpoints[0].id }, { id: 'two', sourceEndpointId: sourceGroups[1].endpoints[0].id, destinationEndpointId: destinationGroups[1].endpoints[0].id }]} onToggleRoute={onToggleRoute} />));
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
        await act(async () => Simulate.click(container.querySelector('[data-port-id="physical:device:input:1"]')));
        expect(container.querySelector('[data-port-id="physical:device:output:1"]').disabled).toBe(true);
        await act(async () => Simulate.click(container.querySelector('[data-port-id="virtual:TimoxVasio:42:input:1"]')));
        expect(onToggleRoute).toHaveBeenCalledWith('physical:device:input:1', 'virtual:TimoxVasio:42:input:1');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('ports expose native graph handles and click routing remains available', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const onToggleRoute = jest.fn();
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[{ id: 'one', sourceEndpointId: sourceGroups[0].endpoints[0].id, destinationEndpointId: destinationGroups[0].endpoints[0].id }, { id: 'two', sourceEndpointId: sourceGroups[1].endpoints[0].id, destinationEndpointId: destinationGroups[1].endpoints[0].id }]} onToggleRoute={onToggleRoute} />));
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
        const source = container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]');
        const destination = container.querySelector('[data-port-id="physical:device:output:1"]');
        expect(source.querySelector('.react-flow__handle-right')).not.toBeNull();
        expect(destination.querySelector('.react-flow__handle-left')).not.toBeNull();
        await act(async () => Simulate.click(source));
        await act(async () => Simulate.click(destination));
        expect(onToggleRoute).toHaveBeenCalledTimes(1);
        expect(onToggleRoute).toHaveBeenCalledWith(source.dataset.portId, destination.dataset.portId);
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('selecting a connection exposes its properties without editing audio', async () => {
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
        expect(document.querySelector('.patchbay-inspector')).toBeNull();
        await chooseContextAction(container.querySelector('[data-node-key="process:app"]'), 'Modify a connection…');
        await act(async () => Simulate.click(document.querySelector('.process-dialog-routes button')));
        expect(onSelectRoute).toHaveBeenCalledWith('route-1');
        expect(onToggleRoute).not.toHaveBeenCalled();
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={routes}
            selectedRouteId="route-1" onSelectRoute={onSelectRoute} onToggleRoute={onToggleRoute}
            onEditRoute={onEditRoute} />));
        expect(document.querySelector('.patchbay-inspector')).not.toBeNull();
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]').classList.contains('route-selected')).toBe(true);
        expect(container.querySelector('[data-port-id="physical:device:output:1"]').classList.contains('route-selected')).toBe(true);
        expect(document.body.textContent).toContain('Selected connection: Out 1 → Output 1');
        await act(async () => Simulate.change(document.querySelector('.route-gain-input'), { target: { value: '-6' } }));
        await act(async () => Simulate.change(document.querySelector('.route-mute-input'), { target: { checked: true } }));
        expect(onEditRoute).toHaveBeenCalledWith('route-1', 'gainDb', -6);
        expect(onEditRoute).toHaveBeenCalledWith('route-1', 'mute', true);
        await act(async () => Simulate.click(document.querySelector('.remove-selected-connection')));
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
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-toolbar-actions button')]
            .find(button => button.textContent === 'Expand all')));
        const groupButton = container.querySelector('[data-group-key="process:app"]');
        expect(groupButton.getAttribute('aria-expanded')).toBe('true');
        await act(async () => Simulate.click(groupButton));
        expect(groupButton.getAttribute('aria-expanded')).toBe('false');
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).toBeNull();
        await act(async () => Simulate.click([...container.querySelectorAll('.patchbay-tools button')]
            .find(button => button.textContent === 'Show a configured connection')));
        expect(container.querySelector('[data-group-key="process:app"]').getAttribute('aria-expanded')).toBe('true');
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).not.toBeNull();
        for (const input of container.querySelectorAll('.patchbay-search input')) {
            await act(async () => Simulate.change(input, { target: { value: '' } }));
        }
        await act(async () => Simulate.click(container.querySelector('[data-group-key="process:app"]')));
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).toBeNull();
        await act(async () => Simulate.change(container.querySelector('.patchbay-search input'), { target: { value: 'Out 1' } }));
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).not.toBeNull();
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('patchbay shows all saved pairs first and can focus one pair', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const secondSource = { id: 'other-app', label: 'Other.exe', kind: 'virtual-output',
        endpoints: [{ id: 'virtual:TimoxVasio:99:output:1', name: 'Other Out 1' }] };
    const secondDestination = { id: 'other-output', label: 'Other interface', kind: 'physical-output',
        endpoints: [{ id: 'physical:other:output:1', name: 'Other Output 1' }] };
    const routes = [
        { id: 'first', sourceEndpointId: sourceGroups[0].endpoints[0].id,
            destinationEndpointId: destinationGroups[0].endpoints[0].id },
        { id: 'second', sourceEndpointId: secondSource.endpoints[0].id,
            destinationEndpointId: secondDestination.endpoints[0].id }
    ];
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={[...sourceGroups, secondSource]}
            destinationGroups={[...destinationGroups, secondDestination]} routes={routes} />));
        expect(container.querySelectorAll('.patchbay-port')).toHaveLength(4);
        expect(container.textContent).toContain('AudioApp.exe');
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:99:output:1"]')).not.toBeNull();
        const focus = container.querySelector('.patchbay-focus select');
        await act(async () => Simulate.change(focus, { target: { value: focus.options[2].value } }));
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:99:output:1"]')).not.toBeNull();
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).toBeNull();
        await act(async () => Simulate.change(focus, { target: { value: '' } }));
        expect(container.querySelectorAll('.patchbay-port')).toHaveLength(4);
        await act(async () => Simulate.change(focus, { target: { value: focus.options[2].value } }));
        await act(async () => root.render(<RoutingPatchbay sourceGroups={[...sourceGroups, secondSource]}
            destinationGroups={[...destinationGroups, secondDestination]} routes={[routes[0]]} />));
        expect(container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:1"]')).not.toBeNull();
        expect(container.querySelector('.patchbay-flow-empty')).toBeNull();
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('unconnected processes show capacity and offer creation without displaying unused ports', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[]} />));
        expect(container.querySelectorAll('.patchbay-port')).toHaveLength(0);
        await act(async () => Simulate.contextMenu(container.querySelector('.patchbay-flow'),
            { clientX: 100, clientY: 100 }));
        expect(document.querySelector('[role="menu"]').textContent).toContain('Create a new connection');
        await act(async () => Simulate.click([...container.querySelectorAll('button')].find(button => button.textContent === 'Expand all')));
        expect(container.querySelectorAll('.patchbay-port')).toHaveLength(0);
        expect(container.querySelector('.patchbay-flow-empty')).toBeNull();
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('a block can be given a local display name with its layout persisted', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    window.localStorage.removeItem('timoxvasio.patchbay.layout.v1');
    const routes = [{ id: 'route-1', sourceEndpointId: sourceGroups[0].endpoints[0].id,
        destinationEndpointId: destinationGroups[0].endpoints[0].id }];
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={routes} />));
        expect(container.querySelector('.react-flow__node-patchbay')).not.toBeNull();
        await chooseContextAction(container.querySelector('[data-node-key="process:app"]'), 'Rename block…');
        await act(async () => Simulate.change(document.querySelector('.patchbay-node-editor input'),
            { target: { value: 'Studio source' } }));
        await act(async () => Simulate.submit(document.querySelector('.patchbay-node-editor')));
        expect(container.querySelector('.patchbay-interface-title').textContent).toContain('Studio source');
        expect(window.localStorage.getItem('timoxvasio.patchbay.layout.v1')).toContain('Studio source');
    } finally {
        await act(async () => root.unmount());
        window.localStorage.removeItem('timoxvasio.patchbay.layout.v1');
        container.remove();
    }
});

test('auto-arrange persists positions, preserves names and does not edit audio connections', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const key = 'timoxvasio.patchbay.layout.v1';
    window.localStorage.setItem(key, JSON.stringify({
        'process:app': { x: -900, y: -800, label: 'Studio source' },
        'hidden-block': { x: 10, y: 20, label: 'Hidden' }
    }));
    const onToggleRoute = jest.fn();
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[]} onToggleRoute={onToggleRoute} />));
        const button = [...container.querySelectorAll('button')].find(item => item.textContent === 'Auto-arrange');
        expect(button.disabled).toBe(false);
        await act(async () => Simulate.click(button));
        const layout = JSON.parse(window.localStorage.getItem(key));
        expect(layout['process:app'].x).toBeGreaterThan(0);
        expect(layout['process:app'].label).toBe('Studio source');
        expect(layout['hidden-block']).toEqual({ x: 10, y: 20, label: 'Hidden' });
        expect(onToggleRoute).not.toHaveBeenCalled();
    } finally {
        await act(async () => root.unmount());
        window.localStorage.removeItem(key);
        container.remove();
    }
});

test('a running process owns its output and saved-profile input in one zoomable block', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const output = { id: 'virtual:TimoxVasio:42:output:1', name: 'scsynth Out 1' };
    const input = { id: 'virtual:TimoxVasio:app:scsynth.exe:input:1', name: 'scsynth In 1 (profile)' };
    const sources = [{ id: 'TimoxVasio:42:output', label: 'scsynth.exe · PID 42',
        kind: 'virtual-output', endpoints: [output] }];
    const destinations = [{ id: 'profile:scsynth.exe:input',
        label: 'scsynth.exe · Saved profile (available when channels open)',
        kind: 'virtual-input', endpoints: [input] }];
    const routes = [{ id: 'loop', sourceEndpointId: output.id, destinationEndpointId: input.id }];
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sources}
            destinationGroups={destinations} routes={routes} />));
        const node = container.querySelector('.patchbay-interface.process');
        expect(container.querySelectorAll('.patchbay-interface')).toHaveLength(1);
        expect(node.querySelector('.patchbay-node-inputs [data-port-id="virtual:TimoxVasio:app:scsynth.exe:input:1"]')).not.toBeNull();
        expect(node.querySelector('.patchbay-node-outputs [data-port-id="virtual:TimoxVasio:42:output:1"]')).not.toBeNull();
        expect(node.textContent).toContain('In 1/1 · Out 1/1 used');
        await act(async () => Simulate.click(container.querySelector('[aria-label="Zoom out"]')));
        expect(container.querySelector('.patchbay-flow-controls')).not.toBeNull();
        await act(async () => Simulate.click(container.querySelector('[aria-label="Zoom in"]')));
        expect(container.querySelector('.patchbay-flow')).not.toBeNull();
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('a selected saved connection exposes its state, gain, mute and scoped matrix', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const route = { id: 'route-1', sourceEndpointId: sourceGroups[0].endpoints[0].id,
        destinationEndpointId: destinationGroups[0].endpoints[0].id, gainDb: 3, mute: true };
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[route]} confirmedRoutes={[route]}
            selectedRouteId={route.id} />));
        expect(container.querySelector('.patchbay-interface.process')).not.toBeNull();
        expect(container.querySelector('.patchbay-interface.physical')).not.toBeNull();
        expect(document.querySelector('.patchbay-inspector')).toBeNull();
        await chooseContextAction(container.querySelector('[data-node-key="process:app"]'), 'Modify a connection…');
        await act(async () => Simulate.click(document.querySelector('.process-dialog-routes button')));
        expect(document.querySelector('.connection-state').textContent).toContain('Saved');
        expect(document.querySelector('.connection-state').textContent).toContain('Muted');
        expect(document.querySelector('.connection-state').textContent).toContain('+3 dB');
        expect(container.querySelector('.connection-matrix')).toBeNull();
        await act(async () => Simulate.click(document.querySelector('.inspect-connection-matrix')));
        expect(document.querySelector('.connection-matrix').textContent).toContain('Out 1');
        expect(document.querySelector('.connection-matrix').textContent).toContain('Output 1');
        expect(document.querySelectorAll('.connection-matrix tbody tr')).toHaveLength(1);
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sourceGroups}
            destinationGroups={destinationGroups} routes={[route]} confirmedRoutes={[]}
            selectedRouteId={route.id} />));
        expect(document.querySelector('.connection-state').textContent).toContain('Pending application');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('global expansion hides unused ports again and counts capacity independently of fan-out and filters', async () => {
    const sources = [{ ...sourceGroups[0], endpoints: [...sourceGroups[0].endpoints,
        { id: 'virtual:TimoxVasio:42:output:2', name: 'Unused output' }] }];
    const routes = destinationGroups.map((group, index) => ({ id: `route-${index}`,
        sourceEndpointId: sources[0].endpoints[0].id, destinationEndpointId: group.endpoints[0].id }));
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container);
    const click = async label => act(async () => Simulate.click([...container.querySelectorAll('button')].find(button => button.textContent === label)));
    const capacity = () => container.querySelector('[data-node-key="process:app"] .patchbay-capacity').textContent;
    const unused = () => container.querySelector('[data-port-id="virtual:TimoxVasio:42:output:2"]');
    try {
        await act(async () => root.render(<RoutingPatchbay sourceGroups={sources} destinationGroups={destinationGroups}
            profiles={[{ processName: 'AudioApp.exe', inputChannels: 12, outputChannels: 8 }]} routes={routes} />));
        expect(capacity()).toBe('In 0/12 · Out 1/8 used');
        expect(unused()).toBeNull();
        await click('Expand all');
        expect(unused()).toBeNull();
        expect(container.querySelector('[data-group-key="process:app"]').getAttribute('aria-expanded')).toBe('true');
        await act(async () => Simulate.change(container.querySelector('.patchbay-search input'), { target: { value: 'Out 1' } }));
        expect(capacity()).toBe('In 0/12 · Out 1/8 used');
        await click('Expand all');
        expect(unused()).toBeNull();
        expect(capacity()).toBe('In 0/12 · Out 1/8 used');
        expect(container.querySelector('.patchbay-search input').value).toBe('');
    } finally { await act(async () => root.unmount()); container.remove(); }
});
