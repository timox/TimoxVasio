import React, { useEffect, useRef } from 'react';
import { createPortal } from 'react-dom';

const states = {
    disabled: ['Disabled', 'Native Windows audio is disabled.'],
    starting: ['Starting', 'Waiting for the audio engine and native stream.'],
    running: ['Running', 'Windows audio is connected to the routing graph.'],
    unavailable: ['Unavailable', 'The native audio service or device is unavailable.'],
    denied: ['Access denied', 'Windows refused access to the native audio stream.'],
    ownedElsewhere: ['In use elsewhere', 'Another session owns the native audio stream.'],
    removed: ['Device removed', 'The native device is no longer available.'],
    failed: ['Failed', 'The native audio stream stopped after an error.']
};
const count = value => Number.isFinite(value) && value >= 0 ? value.toLocaleString() : '—';

export default function NativeAudioDialog({ status, connected, onClose }) {
    const closeButton = useRef(null);
    const close = useRef(onClose);
    close.current = onClose;
    useEffect(() => {
        const previous = document.activeElement;
        closeButton.current?.focus();
        const keydown = event => {
            if (event.key === 'Escape') { event.preventDefault(); close.current(); }
            // Close is the only interactive control in this read-only dialog.
            if (event.key === 'Tab') { event.preventDefault(); closeButton.current?.focus(); }
        };
        document.addEventListener('keydown', keydown);
        return () => {
            document.removeEventListener('keydown', keydown);
            if (previous?.isConnected && previous !== document.body) previous.focus();
            else document.querySelector('.patchbay-flow')?.focus();
        };
    }, []);
    const knownState = Object.prototype.hasOwnProperty.call(states, status?.state) ? states[status.state] : null;
    const [label, description] = !connected ? ['Connection lost', 'Reconnect to the engine to read the current audio state.'] :
        knownState || ['Status unavailable', 'This engine does not report native Windows audio status.'];
    const current = connected && !!knownState;
    const native = current && status.nativeFormat;
    const graph = current && status.state === 'running' && status.graphFormat;
    const health = current && status.health;
    return createPortal(<div className="routing-dialog-backdrop" onMouseDown={event => {
        if (event.target === event.currentTarget) onClose();
    }}><div className="routing-dialog native-audio-dialog" role="dialog" aria-modal="true" aria-label="Native Windows audio">
        <div className="routing-dialog-heading"><h2>Native Windows audio</h2>
            <button type="button" ref={closeButton} onClick={onClose}>Close</button></div>
        <p role="status"><strong>{label}</strong> — {description}</p>
        <p>Windows applications can send audio to the graph and receive audio from it through the native device.</p>
        <dl className="native-audio-details">
            <dt>Requested state</dt><dd>{current ? (status.enabled ? 'Enabled' : 'Disabled') : '—'}</dd>
            <dt>Native stream format</dt><dd>{native ? `${native.sampleRate} Hz · ${native.channels} channels · PCM32` : '—'}</dd>
            <dt>Active graph format</dt><dd>{graph ? `${graph.sampleRate} Hz · ${graph.channels} channels · ${graph.maxQuantum} frames per block` : current ? 'No active native stream' : '—'}</dd>
        </dl>
        {current && status.systemError > 0 && <p>Windows error: {status.systemError}</p>}
        {current && status.retryInMs > 0 && <p>Next retry in {status.retryInMs} ms.</p>}
        {health && <><h3>Stream observations</h3><dl className="native-audio-details">
            <dt>Windows to graph: dropped frames</dt><dd>{count(health.kernelRenderDroppedNativeFrames)}</dd>
            <dt>Graph to Windows: dropped frames</dt><dd>{count(health.kernelCaptureDroppedNativeFrames)}</dd>
            <dt>Discontinuities</dt><dd>{count(health.discontinuities)}</dd>
        </dl><p className="hint">Frame counts use the native sample rate. Observations may remain after the stream stops.</p></>}
        <p className="hint">Status only. Activation is not available in this version.</p>
    </div></div>, document.body);
}
