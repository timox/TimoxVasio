import React, { act } from 'react';
import { createRoot } from 'react-dom/client';
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
        expect(container.querySelector('.analysis-transport .hint').textContent).toContain('share the same');
    } finally {
        await act(async () => root.unmount());
        container.remove();
    }
});
