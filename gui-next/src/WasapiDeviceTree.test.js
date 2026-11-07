import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import WasapiDeviceTree from './WasapiDeviceTree';

test('endpoint tree preserves distinct ports, exposes all channels and opens the selected endpoint', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container); const onAdd = jest.fn();
    const devices = [
        { id: 'in', name: 'Inputs 1–8 (Interface)', flow: 'capture', state: 1, mixFormat: { channels: 8, sampleRate: 48000 } },
        { id: 'out12', name: 'Output pair (Interface)', flow: 'render', state: 1, mixFormat: { channels: 2, sampleRate: 48000 } },
        { id: 'out34', name: 'Output pair (Interface)', flow: 'render', state: 1, mixFormat: { channels: 2, sampleRate: 44100 } },
        { id: 'old', name: 'Disconnected interface', flow: 'render', state: 4, mixFormat: null }
    ];
    try {
        await act(async () => root.render(<WasapiDeviceTree devices={devices} onAdd={onAdd} />));
        expect(container.querySelectorAll('[data-endpoint-id]').length).toBe(3);
        expect(container.querySelector('.wasapi-device-branch').open).toBe(false);
        const input = container.querySelector('[data-endpoint-id="in"]');
        expect(input.textContent).toContain('8 channels');
        expect(input.querySelectorAll('.wasapi-channel-list li').length).toBe(8);
        expect(container.textContent).not.toContain('Disconnected interface');
        const output = container.querySelector('[data-endpoint-id="out34"]');
        await act(async () => Simulate.click(output.querySelector('button')));
        expect(onAdd).toHaveBeenCalledWith(devices[2], 'render');
        await act(async () => Simulate.change(container.querySelector('input[type="checkbox"]'), { target: { checked: true } }));
        expect(container.textContent).toContain('Disconnected interface');
        expect(container.querySelector('[data-endpoint-id="old"]').textContent).toContain('Channels unavailable');
        await act(async () => Simulate.change(container.querySelector('input[type="search"]'), { target: { value: 'Disconnected' } }));
        expect(container.querySelectorAll('[data-endpoint-id]').length).toBe(1);
        expect(container.querySelector('.wasapi-device-branch').open).toBe(true);
        await act(async () => Simulate.change(container.querySelector('input[type="search"]'), { target: { value: 'no-such-device' } }));
        expect(container.textContent).toContain('No devices match this search.');
    } finally { await act(async () => root.unmount()); container.remove(); }
});
