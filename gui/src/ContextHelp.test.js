import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import ContextHelp from './ContextHelp';

test('context help names the field, opens its topic and does not submit its surrounding form', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const host = document.createElement('div'); document.body.appendChild(host);
    const root = createRoot(host);
    const onOpen = jest.fn(), onClick = jest.fn(), onSubmit = jest.fn();
    try {
        await act(async () => root.render(<form onSubmit={onSubmit} onClick={onClick}>
            <ContextHelp topic="wasapi-mode" label="Help with bridge mode" onOpen={onOpen} />
        </form>));
        const button = host.querySelector('button');
        expect(button.type).toBe('button');
        expect(button.getAttribute('aria-label')).toBe('Help with bridge mode');
        expect(button.getAttribute('aria-haspopup')).toBe('dialog');
        await act(async () => Simulate.click(button));
        expect(onOpen).toHaveBeenCalledWith('wasapi-mode');
        expect(onClick).not.toHaveBeenCalled();
        expect(onSubmit).not.toHaveBeenCalled();
    } finally { await act(async () => root.unmount()); host.remove(); }
});
