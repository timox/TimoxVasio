import React from 'react';

const blockTypes = {
    physical: { label: 'ASIO', description: 'Physical ASIO interface', path: <><rect x="2" y="3" width="16" height="14" rx="2" /><path d="M2 10h16M6 6.5h.01M6 13.5h.01M11 6.5h4M11 13.5h4" /></> },
    wasapi: { label: 'WASAPI', description: 'Windows audio bridge', path: <><rect x="2" y="3" width="16" height="14" rx="1" /><path d="M2 7h16M8 7v10" /></> },
    process: { label: 'App', description: 'Application using virtual ASIO channels', path: <><rect x="2" y="3" width="16" height="14" rx="2" /><path d="m8 7 5 3-5 3Z" /></> },
    profile: { label: 'Profile', description: 'Application channel profile', path: <><path d="M5 2h10v16H5Z" /><path d="M8 6h4M8 10h4M8 14h4" /></> },
    saved: { label: 'Saved', description: 'Application is not open', path: <><rect x="2" y="3" width="16" height="14" rx="2" strokeDasharray="3 2" /><path d="M7 10h6" /></> }
};

export function RoutingTypeBadge({ type }) {
    const item = blockTypes[type] || blockTypes.process;
    return <span className={`routing-type-badge ${type}`} title={item.description}>
        <svg viewBox="0 0 20 20" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">{item.path}</svg>
        <span>{item.label}</span>
    </span>;
}

export default function RoutingHelp() {
    return <article className="routing-help">
        <h3>Reading the routing graph</h3>
        <p>A block identifies an interface or application. Its badge names the type; its ports are the channels you can connect.</p>
        <section><h4>Block types</h4>
            <dl className="routing-help-types">
                <dt><RoutingTypeBadge type="physical" /></dt><dd>Your physical ASIO interface. Hardware recording inputs supply audio to the graph; hardware playback outputs receive routed audio. The graph labels below describe the direction at the block, not the names printed on the hardware.</dd>
                <dt><RoutingTypeBadge type="wasapi" /></dt><dd>A bridge to an existing Windows recording or playback endpoint. It uses the Windows driver and adapts the device clock to the graph. Configure the endpoint and mode in Audio setup → WASAPI.</dd>
                <dt><RoutingTypeBadge type="process" /></dt><dd>An application using TimoxVasio’s virtual ASIO driver. Its outputs supply audio; its inputs receive routed audio. This is not a physical ASIO interface.</dd>
                <dt><RoutingTypeBadge type="profile" /></dt><dd>Saved channel capacity for an application. The matching application must open ASIO before those channels can carry audio.</dd>
                <dt><RoutingTypeBadge type="saved" /></dt><dd>Saved connections for an application that is not currently open. They are retained for when its matching channels become available.</dd>
            </dl>
        </section>
        <section><h4>Inputs, outputs and channel counts</h4>
            <p>In the graph, <strong>Outputs</strong> are sources that send audio out of a block; <strong>Inputs</strong> are destinations that receive audio into a block. These labels describe cable direction.</p>
            <p>A hardware recording input such as “Mic 1” therefore appears under <strong>Outputs</strong> in its ASIO block: it supplies recorded audio. A hardware playback output such as “Speakers 1/2” appears under <strong>Inputs</strong>: it receives audio to play. A WASAPI capture bridge likewise supplies Outputs; a WASAPI playback bridge receives Inputs.</p>
            <p><strong>In 2/8 · Out 1/8 used</strong> means two input channels and one output channel are connected, out of eight available in each direction. Sending one output to several destinations still uses one output channel. Hidden or filtered ports do not change these counts.</p>
            <p>The triangle beside a block name expands or collapses its ports. Square sockets belong to physical ASIO; round sockets belong to other channel types. The ASIO/WASAPI/App badge identifies the connection technology without relying on color.</p>
        </section>
        <section><h4>Cables and states</h4>
            <dl className="routing-help-cables">
                <dt><i className="routing-help-line active" aria-hidden="true" />Active</dt><dd>The saved connection is present in the running graph. This does not prove that a signal is audible.</dd>
                <dt><i className="routing-help-line saved" aria-hidden="true" />Saved</dt><dd>The connection is saved but is waiting for its channels to become available.</dd>
                <dt><i className="routing-help-line pending" aria-hidden="true" />Pending</dt><dd>A dashed cable is a draft change. Apply configuration to send it to the engine.</dd>
                <dt><i className="routing-help-line muted" aria-hidden="true" />Muted</dt><dd>A gray dotted cable is muted. The connection stays saved.</dd>
                <dt><i className="routing-help-line selected" aria-hidden="true" />Selected</dt><dd>The cable is emphasized and its ports are outlined. Selection alone opens no properties panel.</dd>
                <dt><span className="routing-help-gain" aria-hidden="true">▲</span>Positive gain</dt><dd>Gain above 0 dB is applied. Open the connection’s context menu to inspect the value.</dd>
            </dl>
            <p>Cable colors can be customized. They do not identify ASIO versus WASAPI and do not prove that audio is flowing. Use the block badge, cable pattern and connection status together.</p>
        </section>
        <section><h4>Actions and layout</h4>
            <p>Right-click the background to add a connection or device. Right-click a block for its actions, or a cable to edit that connection. Detailed settings open in a dialog.</p>
            <p>Auto-arrange moves the blocks; Recenter graph changes the view. Neither changes audio connections. Search and Connection pair only filter what is displayed.</p>
        </section>
    </article>;
}
