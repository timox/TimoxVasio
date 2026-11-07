import React, { useMemo, useState } from 'react';

const timeLabel = time => new Date(time).toLocaleTimeString();

function LineChart({ points, title, color = '#198594', unit = '%' }) {
    const values = points.filter(point => Number.isFinite(point.value));
    const maximum = Math.max(100, ...values.map(point => point.value));
    const first = points[0]?.time || 0;
    const last = points[points.length - 1]?.time || first + 1;
    const x = time => 42 + (time - first) * 534 / Math.max(1, last - first);
    const y = value => 126 - Math.min(maximum, Math.max(0, value)) * 104 / maximum;
    const runs = [];
    let run = [];
    points.forEach(point => {
        if (Number.isFinite(point.value)) run.push(`${x(point.time)},${y(point.value)}`);
        else if (run.length) { runs.push(run); run = []; }
    });
    if (run.length) runs.push(run);
    const latest = values[values.length - 1];
    return <div className="analysis-chart">
        <div className="analysis-chart-heading"><h3>{title}</h3><strong>{latest ? `${latest.value.toFixed(1)}${unit}` : 'No sample yet'}</strong></div>
        <svg viewBox="0 0 600 160" role="img" aria-label={`${title} over time`}>
            <line x1="42" x2="576" y1="126" y2="126" className="analysis-grid" />
            <line x1="42" x2="576" y1={y(100)} y2={y(100)} className="analysis-limit" />
            <text x="2" y="27">{maximum.toFixed(0)}{unit}</text><text x="8" y="130">0</text>
            {runs.map((line, index) => <polyline key={index} points={line.join(' ')} fill="none" stroke={color} strokeWidth="2.5" strokeLinejoin="round" />)}
            {values.length === 1 && <circle cx={x(latest.time)} cy={y(latest.value)} r="4" fill={color} />}
            <text x="42" y="152">{first ? timeLabel(first) : '—'}</text>
            <text x="516" y="152">{last > first ? timeLabel(last) : '—'}</text>
        </svg>
    </div>;
}

function transportDeltas(history, key) {
    let previous;
    return history.filter(item => item.key === key).map(item => {
        const result = { ...item,
            underrunsDelta: previous ? Math.max(0, item.underruns - previous.underruns) : 0,
            overrunsDelta: previous ? Math.max(0, item.overruns - previous.overruns) : 0 };
        previous = item;
        return result;
    });
}

export default function AnalysisTimeline({ performanceHistory, transportHistory, virtualClients, routes, configuredRoutes, engine, onClear, view = 'all', onHelp }) {
    const [selectedPid, setSelectedPid] = useState('');
    const [selectedTransport, setSelectedTransport] = useState('');
    const [span, setSpan] = useState(600000);
    const now = Math.max(Date.now(), performanceHistory.at(-1)?.time || 0, transportHistory.at(-1)?.time || 0);
    const performance = performanceHistory.filter(item => span === 0 || item.time >= now - span);
    const pids = [...new Map(performanceHistory.flatMap(item => item.clients.map(client =>
        [client.pid, client.processName]))).entries()];
    const pid = pids.some(item => String(item[0]) === selectedPid) ? Number(selectedPid) : pids[0]?.[0];
    const transportKeys = [...new Set(transportHistory.map(item => item.key))];
    const key = transportKeys.includes(selectedTransport) ? selectedTransport : transportKeys[0];
    const deltas = useMemo(() => transportDeltas(transportHistory, key), [transportHistory, key]);
    const visibleDeltas = deltas.filter(item => span === 0 || item.time >= now - span);
    const physicalPoints = performance.map(item => ({ time: item.time, value: item.bufferHealth?.blockPeriodNs
        ? item.bufferHealth.worstProcessingNs * 100 / item.bufferHealth.blockPeriodNs : null }));
    const cpuPoints = performance.map(item => ({ time: item.time,
        value: item.clients.find(client => client.pid === pid)?.cpuPercent ?? null }));
    const callbackPoints = performance.map(item => { const callback = item.clients.find(client => client.pid === pid)?.callbackWorstNs;
        return { time: item.time, value: item.sampleRate && item.bufferFrames && callback != null
            ? callback * item.sampleRate / (item.bufferFrames * 10000000) : null };
    });
    const downloadCsv = () => {
        const rows = ['time,type,pid,direction,sample_rate,buffer_frames,physical_callback_percent,delayed_starts,processing_overruns,process_cpu_percent,host_callback_percent,transport_underruns,transport_overruns'];
        performanceHistory.forEach(item => {
            const blockNs = item.sampleRate && item.bufferFrames ? item.bufferFrames * 1e9 / item.sampleRate : null;
            const physical = item.bufferHealth;
            const clients = item.clients.length ? item.clients : [{ pid: '', cpuPercent: null, callbackWorstNs: null }];
            clients.forEach(client => rows.push([
                new Date(item.time).toISOString(), 'performance', client.pid, '', item.sampleRate || '', item.bufferFrames || '',
                physical?.blockPeriodNs ? (physical.worstProcessingNs * 100 / physical.blockPeriodNs).toFixed(2) : '',
                physical?.delayedStartCount ?? '', physical?.processingOverrunCount ?? '', client.cpuPercent ?? '',
                client.callbackWorstNs != null && blockNs ? (client.callbackWorstNs * 100 / blockNs).toFixed(2) : '', '', ''
            ].join(',')));
        });
        transportHistory.forEach(item => rows.push([
            new Date(item.time).toISOString(), 'transport', item.pid, item.direction, '', '', '', '', '', '', '',
            item.underruns, item.overruns
        ].join(',')));
        const url = URL.createObjectURL(new Blob([rows.join('\r\n')], { type: 'text/csv;charset=utf-8' }));
        const link = document.createElement('a');
        link.href = url;
        link.download = `timoxvasio-analysis-${new Date().toISOString().replace(/[:.]/g, '-')}.csv`;
        document.body.appendChild(link);
        link.click();
        link.remove();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    };
    const routed = new Set(routes.flatMap(route => [route.sourceEndpointId, route.destinationEndpointId]));
    return <>
        <section className="section analysis-dashboard" hidden={!['all', 'timeline'].includes(view)}
            id={view !== 'all' ? 'analysis-panel-timeline' : undefined} role={view !== 'all' ? 'tabpanel' : undefined}
            aria-labelledby={view !== 'all' ? 'analysis-tab-timeline' : undefined}>
            <div className="section-subject-heading"><h2>Performance history</h2>
                {onHelp && <button type="button" onClick={() => onHelp('analysis-timeline')}>Reading the timeline</button>}</div>
            <p className="hint">Five-second observations · Current Control session</p>
            <div className="analysis-toolbar">
                <label>Period <select value={span} onChange={event => setSpan(Number(event.target.value))}>
                    <option value={600000}>Last 10 minutes</option><option value={1800000}>Last 30 minutes</option>
                    <option value={0}>Whole session</option></select></label>
                <button type="button" onClick={downloadCsv} disabled={!performanceHistory.length && !transportHistory.length}>Export CSV</button>
                <button type="button" onClick={onClear} disabled={!performanceHistory.length && !transportHistory.length}>Clear session</button>
            </div>
            <div className="analysis-chart-grid">
                <LineChart points={physicalPoints} title={engine?.clockSource?.kind === 'software' ? 'Software callback / block' : 'Physical callback / block'} />
                <div className="analysis-chart-stack">
                    <label>Routed application <select value={pid || ''} onChange={event => setSelectedPid(event.target.value)}>
                        {pids.map(([clientPid, name]) => <option key={clientPid} value={clientPid}>{name} · PID {clientPid}</option>)}
                    </select></label>
                    <LineChart points={cpuPoints} title="Application CPU (all logical processors)" color="#6f5bb4" />
                    <LineChart points={callbackPoints} title="Slowest host callback / block" color="#ad6836" />
                </div>
            </div>
        </section>
        <section className="section analysis-transport" hidden={!['all', 'transport'].includes(view)}
            id={view !== 'all' ? 'analysis-panel-transport' : undefined} role={view !== 'all' ? 'tabpanel' : undefined}
            aria-labelledby={view !== 'all' ? 'analysis-tab-transport' : undefined}>
            <div className="section-subject-heading"><h2>Transport incidents over time</h2>
                {onHelp && <button type="button" onClick={() => onHelp('analysis-transport')}>Understanding incidents</button>}</div>
            <p className="hint">Per application and direction · First sample is the baseline</p>
            <label>Transport <select value={key || ''} onChange={event => setSelectedTransport(event.target.value)}>
                {transportKeys.map(item => { const [transportPid, direction] = item.split(':');
                    const name = virtualClients.find(client => client.pid === Number(transportPid))?.processName || `PID ${transportPid}`;
                    return <option key={item} value={item}>{name} · {direction} · PID {transportPid}</option>;
                })}</select></label>
            {!key ? <p className="hint">Waiting for routed application transport samples.</p> : <div className="incident-timeline" role="img" aria-label="Underruns and overruns by five-second interval">
                {visibleDeltas.slice(-120).map((item, index) => <div key={`${item.time}-${index}`} className="incident-bin"
                    title={`${timeLabel(item.time)} · ${item.underrunsDelta} underruns · ${item.overrunsDelta} overruns`}>
                    <span className="incident-underrun" style={{ height: `${Math.min(100, item.underrunsDelta * 20)}%` }} />
                    <span className="incident-overrun" style={{ height: `${Math.min(100, item.overrunsDelta * 20)}%` }} />
                </div>)}
            </div>}
            <div className="incident-legend"><span>■ Underruns</span><span>■ Overruns</span>
                <strong>{visibleDeltas.reduce((sum, item) => sum + item.underrunsDelta, 0)} / {visibleDeltas.reduce((sum, item) => sum + item.overrunsDelta, 0)} incidents in period</strong></div>
        </section>
        <section className="section analysis-channels" hidden={!['all', 'channels'].includes(view)}
            id={view !== 'all' ? 'analysis-panel-channels' : undefined} role={view !== 'all' ? 'tabpanel' : undefined}
            aria-labelledby={view !== 'all' ? 'analysis-tab-channels' : undefined}>
            <div className="section-subject-heading"><h2>Channels: configured and in use</h2>
                {onHelp && <button type="button" onClick={() => onHelp('analysis-channels')}>Understanding channel states</button>}</div>
            {!virtualClients.length ? <p className="hint">No ASIO application connected.</p> : virtualClients.map(client => {
                const prefix = `virtual:TimoxVasio:${client.pid}:`;
                const directions = [
                    { name: 'Inputs', type: 'input', channels: client.inputChannels || [] },
                    { name: 'Outputs', type: 'output', channels: client.outputChannels || [] }
                ];
                return <div className="analysis-channel-card" key={client.pid}><h3>{client.processName} · PID {client.pid}</h3>
                    {directions.map(direction => { const ids = direction.channels.map(channel => `${prefix}${direction.type}:${channel}`);
                        const configured = new Set(configuredRoutes.flatMap(route => [route.sourceEndpointId, route.destinationEndpointId])
                            .filter(id => id.startsWith(`${prefix}${direction.type}:`)));
                        const active = ids.filter(id => routed.has(id));
                        return <p key={direction.type}><strong>{direction.name}</strong> · {configured.size} configured · {ids.length} open · {active.length} routed now
                            <small>Channels: {active.length ? active.map(id => id.split(':').at(-1)).join(', ') : 'none'}</small></p>;
                    })}</div>;
            })}
            <p className="hint">Engine: {engine?.state || 'unknown'} · Channel activity does not measure signal level.</p>
        </section>
    </>;
}
