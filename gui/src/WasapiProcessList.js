import React from 'react';

export default function WasapiProcessList({ processes = [], bridges = [], busy = false, disabled = false, error = '', onRefresh, onAdd }) {
    const configured = new Set(bridges.filter(bridge => bridge.kind === 'processLoopback')
        .map(bridge => `${bridge.processId}:${bridge.processCreationTime}`));
    return <section className="wasapi-process-list" aria-labelledby="windows-apps-heading">
        <div className="section-heading"><div><h3 id="windows-apps-heading">Process copies</h3>
            <p className="setup-description">Choose a process with active playback to create a separate graph source. Timox copies the sound rendered by that process and its children; it does not copy microphone input the process reads. Original playback continues, and you do not need to change the app’s Windows output.</p>
            <div className="wasapi-process-explanation"><span><strong>Scope:</strong> one process and its children</span><span><strong>Detection:</strong> Windows shared playback</span><span><strong>Routing:</strong> Timox controls the copy</span><span><strong>Limits:</strong> ASIO and WASAPI-exclusive output cannot be copied</span></div></div>
            <button type="button" disabled={busy || disabled} onClick={onRefresh}>{busy ? 'Scanning…' : 'Refresh active processes'}</button></div>
        {error ? <p role="alert">{error}</p> : !processes.length ? <p>No processes with active playback found. Start audio in an application, then refresh.</p> :
            <ul>{processes.map(process => {
                const key = `${process.processId}:${process.creationTime}`;
                const added = configured.has(key);
                return <li key={key}>
                    <div><strong>{process.displayName}</strong><span>Playing through: {process.renderEndpoints.join(', ')}</span>
                        <details><summary>Process identity</summary><code>{process.imagePath}</code></details></div>
                    <button type="button" disabled={disabled || added} onClick={() => onAdd(process)}>{added ? 'Process copy configured' : 'Create process copy'}</button>
                </li>;
            })}</ul>}
    </section>;
}
