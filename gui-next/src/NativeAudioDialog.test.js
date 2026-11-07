import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import NativeAudioDialog from './NativeAudioDialog';

const running = { enabled: true, state: 'running', nativeFormat: { sampleRate: 48000, channels: 2 },
    graphFormat: { sampleRate: 44100, channels: 2, maxQuantum: 128 },
    health: { kernelRenderDroppedNativeFrames: 3, kernelCaptureDroppedNativeFrames: 4, discontinuities: 2 } };

async function withDialog(check) {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const container = document.createElement('div');
    const origin = document.createElement('button');
    document.body.append(origin, container); origin.focus();
    const root = createRoot(container);
    const onClose = jest.fn();
    const show = async (status = running, connected = true) => {
        await act(async () => root.render(<NativeAudioDialog status={status} connected={connected} onClose={onClose} />));
        return document.querySelector('[aria-label="Native Windows audio"]');
    };
    try { await check({ show, onClose, origin }); }
    finally { await act(async () => root.unmount()); expect(document.activeElement).toBe(origin); origin.remove(); container.remove(); }
}

test('native dialog separates native and graph formats and updates state without a stale running claim', async () => {
    await withDialog(async ({ show }) => {
        let dialog = await show();
        expect(dialog.textContent).toContain('48000 Hz');
        expect(dialog.textContent).toContain('44100 Hz');
        expect(dialog.textContent).toContain('128 frames per block');
        dialog = await show({ ...running, state: 'removed' });
        expect(dialog.textContent).toContain('Device removed');
        expect(dialog.textContent).not.toContain('44100 Hz');
        dialog = await show(running, false);
        expect(dialog.textContent).toContain('Connection lost');
        expect(dialog.textContent).not.toContain('Running');
        expect(dialog.textContent).not.toContain('48000 Hz');
        expect(dialog.textContent).not.toContain('No active native stream');
    });
});

test.each([['denied', 'Access denied'], ['ownedElsewhere', 'In use elsewhere'], ['unavailable', 'Unavailable'],
    ['disabled', 'Disabled'], ['starting', 'Starting'], ['failed', 'Failed']])('native dialog describes %s without controls', async (state, label) => {
    await withDialog(async ({ show }) => {
        const dialog = await show({ ...running, state });
        expect(dialog.querySelector('[role="status"]').textContent).toContain(label);
        expect(dialog.querySelectorAll('button')).toHaveLength(1);
        expect(dialog.querySelectorAll('input,select')).toHaveLength(0);
    });
});

test('native dialog treats unsupported state names as unavailable', async () => {
    await withDialog(async ({ show }) => {
        const dialog = await show({ state: 'toString' });
        expect(dialog.textContent).toContain('Status unavailable');
    });
});

test('native dialog supports older engines and traps focus until dismissed', async () => {
    await withDialog(async ({ show, onClose }) => {
        const dialog = await show(null);
        expect(dialog.textContent).toContain('Status unavailable');
        const button = dialog.querySelector('button');
        expect(document.activeElement).toBe(button);
        for (const shiftKey of [false, true]) {
            const event = new KeyboardEvent('keydown', { key: 'Tab', shiftKey, bubbles: true, cancelable: true });
            await act(async () => document.dispatchEvent(event));
            expect(event.defaultPrevented).toBe(true);
            expect(document.activeElement).toBe(button);
        }
        await act(async () => Simulate.click(button));
        expect(onClose).toHaveBeenCalledTimes(1);
        await act(async () => Simulate.mouseDown(dialog.parentElement, { target: dialog.parentElement }));
        expect(onClose).toHaveBeenCalledTimes(2);
    });
});
