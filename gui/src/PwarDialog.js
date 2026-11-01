import React, { useEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';

export default function PwarDialog({ clients = [], activeRoutes = [], connected = true, locked, onClose }) {
    const dialog = useRef(null);
    const close = useRef(onClose); close.current = onClose;
    const clientConfigurationEdited = useRef(false);
    const [settings, setSettings] = useState(null);
    const [windowsAddresses, setWindowsAddresses] = useState([]);
    const [busy, setBusy] = useState(false);
    const [error, setError] = useState('');
    const [message, setMessage] = useState('');
    const [tab, setTab] = useState('client');
    const [clientConfiguration, setClientConfiguration] = useState({ peerAddress: '', maxQueueMs: 100 });
    const [clientStatus, setClientStatus] = useState(null);
    const request = async (method, body) => {
        const response = await window.vasio.httpRequest(method, '/api/v1/pwar', body);
        if (response.status < 200 || response.status >= 300)
            throw new Error(response.body?.message || `PWAR settings unavailable (HTTP ${response.status})`);
        return response.body;
    };
    useEffect(() => {
        let disposed = false;
        request('GET').then(value => { if (!disposed) setSettings(value); })
            .catch(reason => { if (!disposed) setError(reason.message); });
        const refreshClient = async () => {
            try {
                const response = await window.vasio.httpRequest('GET', '/api/v1/pwar/client');
                if (response.status < 200 || response.status >= 300)
                    throw new Error(response.body?.message || `PWAR client unavailable (HTTP ${response.status})`);
                if (!disposed) {
                    const peerAddress = response.body.configuration?.peerAddress ?? response.body.client?.peerAddress;
                    const maxQueueMs = response.body.configuration?.maxQueueMs ?? response.body.client?.maxQueueMs;
                    setClientConfiguration(current => ({
                        peerAddress: clientConfigurationEdited.current ? current.peerAddress : peerAddress ?? current.peerAddress,
                        maxQueueMs: clientConfigurationEdited.current ? current.maxQueueMs : maxQueueMs ?? current.maxQueueMs
                    }));
                    setClientStatus(response.body.client || response.body);
                }
            } catch (reason) { if (!disposed) setError(reason.message); }
        };
        refreshClient();
        const poll = window.setInterval(refreshClient, 1500);
        window.vasio.getNetworkAddresses?.().then(value => {
            if (!disposed) setWindowsAddresses(Array.isArray(value) ? value : []);
        }).catch(() => {});
        const previous = document.activeElement;
        dialog.current.querySelector('button').focus();
        const keydown = event => {
            if (event.key === 'Escape') { event.preventDefault(); close.current(); }
            if (event.key === 'Tab') {
                const controls = [...dialog.current.querySelectorAll(
                    'a[href], button:not(:disabled), input:not(:disabled), select:not(:disabled), textarea:not(:disabled), summary, [tabindex]:not([tabindex="-1"])'
                )].filter(control => control.getClientRects().length > 0);
                const index = controls.indexOf(document.activeElement);
                event.preventDefault();
                const next = index < 0 ? (event.shiftKey ? controls.length - 1 : 0) :
                    (index + (event.shiftKey ? -1 : 1) + controls.length) % controls.length;
                controls[next]?.focus();
            }
        };
        document.addEventListener('keydown', keydown);
        return () => { disposed = true; window.clearInterval(poll); document.removeEventListener('keydown', keydown);
            if (previous?.isConnected) previous.focus(); };
    }, []);
    const perform = async action => {
        setBusy(true); setError(''); setMessage('');
        try { await action(); } catch (reason) { setError(reason.message); }
        finally { setBusy(false); }
    };
    const pwarClients = connected ? clients.filter(client => /pwar/i.test(client.processName)) : [];
    const disabled = busy || locked || !connected;
    const change = value => { setSettings(value); setMessage(''); };
    const clientState = clientStatus?.state || 'stopped';
    const clientRunning = clientStatus?.available === true && !['stopped', 'error'].includes(clientState);
    const runClientCommand = async (command, payload) => perform(async () => {
        const result = await window.vasio.executeCommand({
            id: `pwar-client-${command}-${Date.now()}`, command,
            ...(payload ? { payload } : {})
        });
        if (result?.client) {
            setClientStatus(result.client);
            clientConfigurationEdited.current = false;
            setClientConfiguration(current => ({ peerAddress: result.client.peerAddress || current.peerAddress,
                maxQueueMs: result.client.maxQueueMs || current.maxQueueMs }));
        }
        else if (result?.state) setClientStatus(result);
        setMessage(command === 'pwar.client.start' ? 'PWAR ASIO client started.' : 'PWAR ASIO client stopped.');
    });
    return createPortal(<div className="routing-dialog-backdrop"><div ref={dialog}
        className="routing-dialog" role="dialog" aria-modal="true" aria-label="PWAR / PipeWire">
        <div className="routing-dialog-heading"><h2>PWAR / PipeWire</h2><button onClick={onClose}>Close</button></div>
        <div className="pwar-dialog-tabs" role="tablist" aria-label="PWAR controls">
            <button type="button" role="tab" aria-selected={tab === 'client'} onClick={() => setTab('client')}>Supervised ASIO client</button>
            <button type="button" role="tab" aria-selected={tab === 'legacy'} onClick={() => setTab('legacy')}>Legacy driver settings</button>
        </div>
        {tab === 'client' && <section className="pwar-direction" aria-labelledby="pwar-client-heading">
            <h3 id="pwar-client-heading">Windows ↔ Linux audio bridge</h3>
            <p>This supervised client joins the Timox audio graph through the existing ASIO driver. Its inputs send Timox audio to Linux; its outputs return Linux audio to Timox. The physical audio device stays selected in Audio setup.</p>
            <div className="pwar-client-state" role="status"><span className={`pwar-state-dot ${clientRunning ? 'running' : clientState === 'error' ? 'error' : ''}`} />
                <strong>{clientState}</strong>{clientStatus?.pid && <span>PID {clientStatus.pid}</span>}
                {clientStatus?.error && <span role="alert">{clientStatus.error}</span>}
            </div>
            <label className="field">Linux peer IPv4 address<input inputMode="decimal" autoComplete="off"
                value={clientConfiguration.peerAddress} disabled={disabled || clientRunning}
                onChange={event => {
                    clientConfigurationEdited.current = true;
                    setClientConfiguration(current => ({ ...current, peerAddress: event.target.value }));
                }} /></label>
            <label className="field">Maximum queued audio age (ms)<input type="number" min="1" max="2000" step="1"
                value={clientConfiguration.maxQueueMs} disabled={disabled || clientRunning}
                onChange={event => {
                    clientConfigurationEdited.current = true;
                    setClientConfiguration(current => ({ ...current, maxQueueMs: event.target.value }));
                }} />
                <span className="hint">Higher values tolerate more network jitter but can increase delay. Start around 100 ms.</span></label>
            <div className="pwar-client-actions">
                <button disabled={disabled || clientRunning || !clientConfiguration.peerAddress ||
                    !Number.isInteger(Number(clientConfiguration.maxQueueMs)) || Number(clientConfiguration.maxQueueMs) < 1 || Number(clientConfiguration.maxQueueMs) > 2000}
                    onClick={() => runClientCommand('pwar.client.start', { peerAddress: clientConfiguration.peerAddress.trim(), maxQueueMs: Number(clientConfiguration.maxQueueMs) })}>Start client</button>
                <button disabled={disabled || !clientRunning} onClick={() => runClientCommand('pwar.client.stop')}>Stop client</button>
                <button disabled={busy || !connected} onClick={() => window.vasio.httpRequest('GET', '/api/v1/pwar/client').then(response => {
                    if (response.status < 200 || response.status >= 300) throw new Error(response.body?.message || 'Unable to refresh PWAR client status');
                    setClientStatus(response.body.client || response.body);
                }).catch(reason => setError(reason.message))}>Refresh status</button>
            </div>
            {clientStatus?.status && <dl className="pwar-client-metrics">
                <div><dt>ASIO callbacks</dt><dd>{clientStatus.status.callbackCount ?? '—'}</dd></div>
                <div><dt>Network packets</dt><dd>{[clientStatus.status.packetsSent, clientStatus.status.packetsReceived].some(Number.isFinite)
                    ? `${clientStatus.status.packetsSent ?? 0} sent · ${clientStatus.status.packetsReceived ?? 0} received` : '—'}</dd></div>
                <div><dt>Last activity</dt><dd>{clientStatus.status.lastActivity ?? '—'}</dd></div>
                <div><dt>Latency / drift</dt><dd>Not measured</dd></div>
            </dl>}
            <p className="hint">A running status means the client has opened its ASIO stream. Confirm signal with the graph meters; timing and end-to-end latency are not guaranteed by the current counters.</p>
        </section>}
        {tab === 'legacy' && <>
        <p>For the Timox integration, the PWAR Windows bridge must open <code>TimoxVasio.dll</code> as an ASIO client. When running in this mode, it appears as an ordinary App block in Routing; the physical ASIO device selected by Timox remains your local audio interface.</p>
        <p>Route a Timox source to the PWAR client’s <strong>Inputs</strong> to send Windows audio to Linux. Route the PWAR client’s <strong>Outputs</strong> to a destination to bring Linux audio into Windows. Only these explicit graph routes carry audio.</p>
        <p role="status">{!connected ? 'Connection lost' : pwarClients.length ? `${pwarClients.length} ASIO client${pwarClients.length === 1 ? '' : 's'} with “PWAR” in the process name detected.` : 'No ASIO client with “PWAR” in the process name is detected.'}
            {' '}This identification uses the process name; it is not a PWAR handshake. Detection confirms a driver connection only; route counts do not confirm signal or network traffic. Check the channel meters in Routing to confirm audio reaches Timox.</p>
        {!connected ? <p className="hint">Reconnect to Timox to refresh the ASIO client list.</p> : pwarClients.length > 0 ? <ul className="pwar-client-list">{pwarClients.map(client => {
            const prefix = `virtual:TimoxVasio:${client.pid}:`;
            const routedInputs = new Set(activeRoutes.filter(route => route.destinationEndpointId.startsWith(`${prefix}input:`))
                .map(route => route.destinationEndpointId)).size;
            const routedOutputs = new Set(activeRoutes.filter(route => route.sourceEndpointId.startsWith(`${prefix}output:`))
                .map(route => route.sourceEndpointId)).size;
            return <li key={client.pid}>
                <strong>{client.processName}</strong><span>PID {client.pid} · {client.inputChannels?.length || 0} input channels · {client.outputChannels?.length || 0} output channels</span>
                <span>Configured graph routes: {routedInputs} input channels · {routedOutputs} output channels</span>
            </li>;
        })}</ul> : <p className="hint">If the Windows bridge is connected under a process name without “PWAR”, it will still appear as an ordinary ASIO App block in Routing but will not be listed here. The bridge must open TimoxVasio.dll as an ASIO client; the physical ASIO device remains selected independently.</p>}
        {settings && <section className="pwar-direction" aria-labelledby="pwar-send-heading">
            <h3 id="pwar-send-heading">Windows → Linux · send audio</h3>
            <p>These fields set the Linux destination written to the Windows PWAR ASIO configuration.</p>
            <label className="field">Linux destination IPv4 address<input value={settings.address} disabled={disabled}
                onChange={event => change({ ...settings, address: event.target.value })} /></label>
            <label className="field">Linux destination UDP port<input type="number" min="1" max="65535" value={settings.port} disabled={disabled}
                onChange={event => change({ ...settings, port: event.target.value })} /></label>
            <button disabled={disabled} onClick={() => perform(async () => {
                const result = await request('PUT', { address: settings.address, port: Number(settings.port) });
                setMessage(result.reopenRequired
                    ? 'Saved. Restart the PWAR Windows client to load the network settings.'
                    : 'Saved.');
            })}>Save network settings</button>
            <p className="hint">Saved in the engine user's pwarASIO.cfg. Saving does not start audio. A patched PWAR driver is required for a custom destination port; older versions send to port 8321.</p>
        </section>}
        <section className="pwar-direction" aria-labelledby="pwar-receive-heading">
            <h3 id="pwar-receive-heading">Linux → Windows · receive audio</h3>
            <p>Windows PWAR listens on UDP port <strong>8321</strong>. This port is fixed by the current driver.</p>
            <p>In the Linux PWAR interface, set the target to one of these Windows IPv4 addresses and port 8321:</p>
            {windowsAddresses.length ? <ul className="pwar-address-list">{windowsAddresses.map(({ name, address }) => <li key={`${name}-${address}`}><span>{name}: </span><code>{address}</code></li>)}</ul> :
                <p className="hint">No active Windows IPv4 address was detected. Check the address of the network adapter shared with Linux.</p>}
            <details className="pwar-linux-help">
                <summary>What do the Linux audio settings mean?</summary>
                <dl>
                    <dt>Buffer size (frames)</dt><dd>Frames in each audio block. At 48 kHz, 64 frames represent about 1.33 ms per block; this is not the total round-trip latency. Smaller blocks can reduce delay but leave less room for scheduling and network jitter. Start at 64; raise it if you hear dropouts.</dd>
                    <dt>Sample rate</dt><dd>Samples per second. Choose a rate supported by the Windows ASIO host and Linux PipeWire devices; 48 kHz is a practical starting point when both sides support it.</dd>
                    <dt>Oneshot mode</dt><dd>Sends each block as a single packet to reduce buffering. It can increase CPU load and is less forgiving of timing jitter.</dd>
                    <dt>Variable buffer sizes</dt><dd>Allows block sizes to change at runtime so you can balance latency and stability. Behavior depends on the Linux PWAR implementation; check the meters and listen for dropouts after changing it.</dd>
                </dl>
                <p className="hint">These options are configured in PWAR on Linux. This Windows panel currently cannot change them remotely.</p>
            </details>
        </section>
        <p className="hint">These network fields save the Windows PWAR configuration only; they do not start or restart the client and do not report network or audio activity. Restart the PWAR Windows client after saving if it requires reopening to load the new settings.</p>
        </>}
        {error && <p role="alert">{error}</p>}
        {message && <p role="status">{message}</p>}
    </div></div>, document.body);
}
