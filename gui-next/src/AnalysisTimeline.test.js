import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import AnalysisTimeline from './AnalysisTimeline';

test('analysis distinguishes cumulative transport counters from incidents and reports channel state', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const time = Date.now();
    const endpoint = 'virtual:TimoxVasio:42:output:1';
    const container = document.createElement('div');
    document.body.appendChild(container);
    const root = createRoot(container);
    try {
        await act(async () => root.render(<AnalysisTimeline
            performanceHistory={[{ time: time - 5000, sampleRate: 48000, bufferFrames: 480,
                bufferHealth: { blockPeriodNs: 10000000, worstProcessingNs: 12000000 },
                clients: [{ pid: 42, processName: 'AudioApp.exe', cpuPercent: 6.25, callbackWorstNs: null }] }]}
            transportHistory={[
                { time: time - 5000, key: '42:output', pid: 42, direction: 'output', underruns: 10, overruns: 3 },
                { time, key: '42:output', pid: 42, direction: 'output', underruns: 12, overruns: 4 }
            ]}
            virtualClients={[{ pid: 42, processName: 'AudioApp.exe', inputChannels: [], outputChannels: [1, 2] }]}
            configuredRoutes={[{ sourceEndpointId: endpoint, destinationEndpointId: 'physical:device:output:1' }]}
            routes={[{ sourceEndpointId: endpoint, destinationEndpointId: 'physical:device:output:1' }]}
            engine={{ state: 'running' }} onClear={() => {}} />));
        expect(container.querySelector('.analysis-chart-heading')?.textContent).toContain('120.0%');
        expect(container.querySelectorAll('.analysis-chart')[2].textContent).toContain('No sample yet');
        expect(container.querySelector('.incident-legend').textContent).toContain('2 / 1 incidents');
        expect(container.querySelector('.analysis-channel-card').textContent).toContain('1 configured · 2 open · 1 routed now');
        expect(container.querySelector('.analysis-transport .hint').textContent).toContain('First sample is the baseline');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});

test('analysis subject changes preserve timeline controls and show only the requested measurements', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container); const onHelp = jest.fn();
    const props = { performanceHistory: [], transportHistory: [], virtualClients: [], routes: [], configuredRoutes: [],
        engine: { clockSource: { kind: 'software' } }, onClear: () => {}, onHelp };
    try {
        await act(async () => root.render(<AnalysisTimeline {...props} view="timeline" />));
        expect(container.querySelector('.analysis-dashboard').hidden).toBe(false);
        expect(container.querySelector('.analysis-transport').hidden).toBe(true);
        expect(container.querySelector('.analysis-chart-heading').textContent).toContain('Software callback');
        const period = container.querySelector('.analysis-toolbar select');
        await act(async () => Simulate.change(period, { target: { value: '0' } }));
        await act(async () => root.render(<AnalysisTimeline {...props} view="channels" />));
        expect(container.querySelector('.analysis-dashboard').hidden).toBe(true);
        expect(container.querySelector('.analysis-channels').hidden).toBe(false);
        expect(container.querySelector('.analysis-channels').id).toBe('analysis-panel-channels');
        expect(container.querySelector('.analysis-channels').getAttribute('aria-labelledby')).toBe('analysis-tab-channels');
        await act(async () => Simulate.click(container.querySelector('.analysis-channels button')));
        expect(onHelp).toHaveBeenCalledWith('analysis-channels');
        await act(async () => root.render(<AnalysisTimeline {...props} view="timeline" />));
        expect(container.querySelector('.analysis-toolbar select').value).toBe('0');
    } finally { await act(async () => root.unmount()); container.remove(); }
});
