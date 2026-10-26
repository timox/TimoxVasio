import React, { useEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';

export default function PwarDialog({ drivers, activeDriver, running, connected = true, locked,
    canReopen, onSelect, onReopen, onClose }) {
    const dialog = useRef(null);
    const close = useRef(onClose); close.current = onClose;
    const [settings, setSettings] = useState(null);
    const [saved, setSaved] = useState(null);
    const [busy, setBusy] = useState(false);
    const [error, setError] = useState('');
    const [message, setMessage] = useState('');
    const request = async (method, body) => {
        const response = await window.vasio.httpRequest(method, '/api/v1/pwar', body);
        if (response.status < 200 || response.status >= 300)
            throw new Error(response.body?.message || `PWAR settings unavailable (HTTP ${response.status})`);
        return response.body;
    };
    useEffect(() => {
        let disposed = false;
        request('GET').then(value => { if (!disposed) { setSettings(value); setSaved(value); } })
            .catch(reason => { if (!disposed) setError(reason.message); });
        const previous = document.activeElement;
        dialog.current.querySelector('button').focus();
        const keydown = event => {
            if (event.key === 'Escape') { event.preventDefault(); close.current(); }
            if (event.key === 'Tab') {
                const controls = [...dialog.current.querySelectorAll('button:not(:disabled), input:not(:disabled)')];
                const index = controls.indexOf(document.activeElement);
                event.preventDefault();
                controls[(index + (event.shiftKey ? -1 : 1) + controls.length) % controls.length]?.focus();
            }
        };
        document.addEventListener('keydown', keydown);
        return () => { disposed = true; document.removeEventListener('keydown', keydown);
            if (previous?.isConnected) previous.focus(); };
    }, []);
    const perform = async action => {
        setBusy(true); setError(''); setMessage('');
        try { await action(); } catch (reason) { setError(reason.message); }
        finally { setBusy(false); }
    };
    const pwar = drivers.filter(driver => /pwar/i.test(driver.name));
    const active = /pwar/i.test(activeDriver?.name || '');
    const disabled = busy || locked || !connected;
    const unsaved = settings && (settings.address !== saved?.address || Number(settings.port) !== saved?.port);
    const change = value => { setSettings(value); setMessage(''); };
    return createPortal(<div className="routing-dialog-backdrop"><div ref={dialog}
        className="routing-dialog" role="dialog" aria-modal="true" aria-label="PWAR / PipeWire">
        <div className="routing-dialog-heading"><h2>PWAR / PipeWire</h2><button onClick={onClose}>Close</button></div>
        <p>Send audio to Linux through the PWAR ASIO driver. Windows receives PWAR audio on UDP port 8321.</p>
        <p role="status">{!connected ? 'Connection lost' : active && running ? 'PWAR driver open' : 'PWAR driver is not running'}.
            {' '}Driver state does not confirm audio reception from Linux. Check the routing meters during playback.</p>
        {error && <p role="alert">{error}</p>}
        {message && <p role="status">{message}</p>}
        {settings && <>
            <label className="field">Linux IPv4 address<input value={settings.address} disabled={disabled}
                onChange={event => change({ ...settings, address: event.target.value })} /></label>
            <label className="field">Linux UDP port<input type="number" min="1" max="65535" value={settings.port} disabled={disabled}
                onChange={event => change({ ...settings, port: event.target.value })} /></label>
            <button disabled={disabled} onClick={() => perform(async () => {
                await request('PUT', { address: settings.address, port: Number(settings.port) });
                setSaved({ address: settings.address, port: Number(settings.port) });
                setMessage('Saved. Reopen the PWAR driver to use these settings.');
            })}>Save network settings</button>
        </>}
        <p className="hint">Settings are saved in the engine user's pwarASIO.cfg. Saving does not start audio.
            The destination port requires the patched PWAR driver; older versions use port 8321.</p>
        {!pwar.length ? <p>PWAR driver is not installed or was not discovered. Install it before testing audio.</p> :
            pwar.map(driver => <button key={driver.id} disabled={disabled} onClick={() => {
                onSelect(driver.id); setMessage('Driver selected in the draft. Close this window and apply configuration to activate it.');
            }}>Select {driver.name}</button>)}
        <button disabled={disabled || !settings || unsaved || !active || !canReopen} onClick={() => perform(async () => {
            await onReopen(); setMessage('PWAR driver reopened. Check the routing meters while Linux sends audio.');
        })}>Reopen PWAR driver</button>
        <p className="hint">Save network settings and apply or discard pending configuration changes before reopening. Reopening interrupts audio.</p>
    </div></div>, document.body);
}
