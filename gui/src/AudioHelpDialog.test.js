import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import AudioHelpDialog from './AudioHelpDialog';

async function withHelp(check) {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const host = document.createElement('div');
    const origin = document.createElement('button');
    document.body.append(origin, host);
    origin.focus();
    const root = createRoot(host);
    const onClose = jest.fn();
    const show = async topic => {
        await act(async () => root.render(<AudioHelpDialog topic={topic} onClose={onClose} />));
        return document.querySelector('.audio-help-dialog');
    };
    try { await check({ show, onClose, origin }); }
    finally {
        await act(async () => root.unmount());
        expect(document.activeElement).toBe(origin);
        host.remove(); origin.remove();
    }
}

test('help keeps keyboard focus in the dialog, dismisses with Escape and restores its trigger', async () => {
    await withHelp(async ({ show, onClose, origin }) => {
        const dialog = await show('clock');
        const button = dialog.querySelector('button');
        const body = dialog.querySelector('[role="region"]');
        expect(document.activeElement).toBe(button);
        expect(document.getElementById(dialog.getAttribute('aria-labelledby')).textContent).toBe('Audio clock');
        const previous = new KeyboardEvent('keydown', { key: 'Tab', shiftKey: true, bubbles: true, cancelable: true });
        await act(async () => document.dispatchEvent(previous));
        expect(previous.defaultPrevented).toBe(true);
        expect(document.activeElement).toBe(body);
        const next = new KeyboardEvent('keydown', { key: 'Tab', bubbles: true, cancelable: true });
        await act(async () => document.dispatchEvent(next));
        expect(next.defaultPrevented).toBe(true);
        expect(document.activeElement).toBe(button);
        origin.focus();
        expect(document.activeElement).toBe(button);
        await act(async () => document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })));
        expect(onClose).toHaveBeenCalledTimes(1);
    });
});

test('clicking content keeps help open while Close and the backdrop dismiss it', async () => {
    await withHelp(async ({ show, onClose }) => {
        const dialog = await show('wasapi');
        await act(async () => Simulate.mouseDown(dialog));
        expect(onClose).not.toHaveBeenCalled();
        await act(async () => Simulate.click(dialog.querySelector('button')));
        expect(onClose).toHaveBeenCalledTimes(1);
        await act(async () => Simulate.mouseDown(dialog.parentElement));
        expect(onClose).toHaveBeenCalledTimes(2);
    });
});

test.each([
    ['clock', 'not MIDI tempo', 'WASAPI is not a selectable master'],
    ['wasapi', 'audio disabled', 'not bit-perfect'],
    ['asio', 'autonomous virtual ASIO driver', 'opens ASIO again'],
    ['performance', 'all logical processors', 'End-to-end latency is not measured']
])('%s help separates its explanations from live settings', async (topic, first, second) => {
    await withHelp(async ({ show }) => {
        const dialog = await show(topic);
        expect(dialog.textContent).toContain(first);
        expect(dialog.textContent).toContain(second);
        expect(dialog.querySelectorAll('input, select')).toHaveLength(0);
    });
});
