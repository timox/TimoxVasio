import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import PwarDialog from './PwarDialog';

let root, host;
beforeEach(() => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    host = document.createElement('div'); document.body.append(host);
    root = createRoot(host);
});
afterEach(async () => {
    await act(async () => root.unmount()); host.remove(); delete window.vasio;
});
async function render(status, props = {}) {
    window.vasio = { httpRequest: jest.fn(async (method, path) => ({ status: 200,
        body: path.endsWith('/client') ? { client: status } : { address: '', port: 8321 } })) };
    await act(async () => root.render(<PwarDialog onClose={() => {}} {...props} />));
}
async function legacy() {
    await act(async () => Simulate.click([...document.querySelectorAll('button')]
        .find(button => button.textContent === 'Legacy driver settings')));
}
test('PWAR uses the supervisor PID rather than the process name', async () => {
    await render({ available: true, state: 'running', pid: 12 }, { clients: [
        { pid: 11, processName: 'pwar-impostor.exe' }, { pid: 12, processName: 'bridge.exe' }
    ] });
    await legacy();
    expect(document.querySelector('.pwar-client-list').textContent).toContain('bridge.exe');
    expect(document.querySelector('.pwar-client-list').textContent).not.toContain('pwar-impostor');
});
test.each([
    null,
    { available: true, state: 'stopped', pid: 12 },
    { available: true, state: 'running' }
])('no PWAR identity is fabricated for missing status, stopped state or missing PID: %s', async status => {
    await render(status, { clients: [{ pid: 12, processName: 'pwar.exe' }] });
    await legacy();
    expect(document.querySelector('.pwar-client-list')).toBeNull();
});
test('missing state and packet counts remain unknown', async () => {
    await render({ status: { packetsSent: 4 } });
    expect(document.body.textContent).toContain('Status unavailable');
    expect(document.body.textContent).toContain('4 sent · — received');
});
test('disconnection hides previous observations', async () => {
    await render({ available: true, state: 'running', pid: 12, status: { packetsSent: 4 } }, { connected: false });
    expect(document.body.textContent).toContain('Connection lost');
    expect(document.querySelector('.pwar-client-metrics')).toBeNull();
    expect(document.body.textContent).not.toContain('PID 12');
});
test('a failed status refresh clears the previous runtime claim', async () => {
    await render({ available: true, state: 'running', pid: 12, status: { packetsSent: 4 } });
    window.vasio.httpRequest.mockResolvedValueOnce({ status: 503, body: { message: 'Unavailable' } });
    await act(async () => Simulate.click([...document.querySelectorAll('button')]
        .find(button => button.textContent === 'Refresh status')));
    expect(document.body.textContent).toContain('Status unavailable');
    expect(document.querySelector('.pwar-client-metrics')).toBeNull();
    expect(document.body.textContent).not.toContain('PID 12');
});
test('reconnection waits for fresh supervisor observations', async () => {
    await render({ available: true, state: 'running', pid: 12, status: { packetsSent: 4 } });
    let finishRefresh;
    window.vasio.httpRequest.mockImplementationOnce(() => new Promise(resolve => { finishRefresh = resolve; }));
    await act(async () => Simulate.click([...document.querySelectorAll('button')]
        .find(button => button.textContent === 'Refresh status')));
    await act(async () => root.render(<PwarDialog onClose={() => {}} connected={false} />));
    await act(async () => finishRefresh({ status: 200, body: { client: {
        available: true, state: 'running', pid: 12, status: { packetsSent: 4 }
    } } }));
    window.vasio.httpRequest.mockImplementation(() => new Promise(() => {}));
    await act(async () => root.render(<PwarDialog onClose={() => {}} connected />));
    expect(document.body.textContent).toContain('Status unavailable');
    expect(document.querySelector('.pwar-client-metrics')).toBeNull();
    expect(document.body.textContent).not.toContain('PID 12');
});
