import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import PwarDialog from './PwarDialog';

test('PWAR settings show HTTP failures and saving never reopens audio', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const host = document.createElement('div'); document.body.append(host);
    const root = createRoot(host);
    const reopen = jest.fn();
    window.vasio = { httpRequest: jest.fn().mockResolvedValueOnce({ status: 200,
        body: { address: '192.168.66.2', port: 8321 } })
        .mockResolvedValueOnce({ status: 500, body: { message: 'Access denied' } })
        .mockResolvedValueOnce({ status: 200, body: { exists: true } }) };
    try {
        await act(async () => root.render(<PwarDialog drivers={[]} onClose={() => {}} onReopen={reopen} />));
        expect(document.body.textContent).toContain('PWAR driver is not installed');
        const save = [...document.querySelectorAll('button')].find(button => button.textContent === 'Save network settings');
        await act(async () => Simulate.click(save));
        expect(document.querySelector('[role="alert"]').textContent).toBe('Access denied');
        await act(async () => Simulate.click(save));
        expect(document.body.textContent).toContain('Saved. Reopen');
        expect(reopen).not.toHaveBeenCalled();
        expect(window.vasio.httpRequest).toHaveBeenLastCalledWith('PUT', '/api/v1/pwar', { address: '192.168.66.2', port: 8321 });
    } finally { await act(async () => root.unmount()); host.remove(); delete window.vasio; }
});

test('PWAR reopen reports failures and is blocked while network settings are unsaved', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const host = document.createElement('div'); document.body.append(host);
    const root = createRoot(host);
    const reopen = jest.fn().mockRejectedValue(new Error('Driver could not start'));
    const driver = { id: 'pwar', name: 'PWAR ASIO' };
    window.vasio = { httpRequest: jest.fn().mockResolvedValue({ status: 200,
        body: { address: '192.168.66.2', port: 8321 } }) };
    try {
        await act(async () => root.render(<PwarDialog drivers={[driver]} activeDriver={driver}
            running canReopen onClose={() => {}} onReopen={reopen} />));
        expect(document.body.textContent).toContain('Driver state does not confirm audio reception');
        const button = [...document.querySelectorAll('button')].find(item => item.textContent === 'Reopen PWAR driver');
        await act(async () => Simulate.click(button));
        expect(document.querySelector('[role="alert"]').textContent).toBe('Driver could not start');
        const input = document.querySelector('input');
        await act(async () => Simulate.change(input, { target: { value: '192.168.66.3' } }));
        expect(button.disabled).toBe(true);
    } finally { await act(async () => root.unmount()); host.remove(); delete window.vasio; }
});
