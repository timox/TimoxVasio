import React from 'react';
import AudioFlowIcon from './AudioFlowIcon';

const blockTypes = {
    physical: { label: 'ASIO device', description: 'Physical ASIO audio interface', path: <><rect x="2" y="3" width="16" height="14" rx="2" /><path d="M2 10h16M6 6.5h.01M6 13.5h.01M11 6.5h4M11 13.5h4" /></> },
    wasapi: { label: 'WASAPI device', description: 'Timox opens an existing Windows audio endpoint through WASAPI', path: <><rect x="2" y="3" width="16" height="14" rx="1" /><path d="M2 7h16M8 7v10" /></> },
    wasapiApp: { label: 'Process copy', description: 'Timox copies shared Windows audio from this process tree into a separate graph source; original playback continues and the process does not become an ASIO client', path: <><rect x="4" y="2" width="12" height="12" rx="1" /><rect x="2" y="6" width="12" height="12" rx="1" /><path d="M10 10h6m-2-2 2 2-2 2" /></> },
    process: { label: 'Timox ASIO', description: 'Direct ASIO client: this application opened the TimoxVasio driver', path: <><rect x="2" y="3" width="16" height="14" rx="2" /><path d="m8 7 5 3-5 3Z" /></> },
    profile: { label: 'Profile', description: 'Application channel profile', path: <><path d="M5 2h10v16H5Z" /><path d="M8 6h4M8 10h4M8 14h4" /></> },
    saved: { label: 'Saved', description: 'Application is not open', path: <><rect x="2" y="3" width="16" height="14" rx="2" strokeDasharray="3 2" /><path d="M7 10h6" /></> }
};

export function RoutingTypeBadge({ type, direction }) {
    const item = blockTypes[type] || blockTypes.process;
    const flow = direction === 'copyIntoTimox' || type === 'wasapiApp'
        ? { icon: 'copy', label: 'COPY' }
        : direction === 'inputIntoTimox' ? { icon: 'input', label: 'IN' }
        : direction === 'outOfTimox' ? { icon: 'output', label: 'OUT' }
        : direction === 'duplex' ? { icon: 'duplex', label: 'IN/OUT' } : null;
    return <span className={`routing-type-badge ${type}`} title={item.description}>
        {flow ? <span className={`routing-flow-direction ${flow.icon}`}><AudioFlowIcon kind={flow.icon} /><span>{flow.label}</span></span> : <>
            <svg viewBox="0 0 20 20" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">{item.path}</svg>
            <span>{item.label}</span>
        </>}
    </span>;
}

export default function RoutingHelp() {
    return <article className="routing-help">
        <h3>Reading the routing graph</h3>
        <p>A block identifies an interface or application. Its badge names the type; its ports are the channels you can connect.</p>
        <section><h4>Block types</h4>
            <dl className="routing-help-types">
                <dt><RoutingTypeBadge type="physical" /></dt><dd>Your physical ASIO interface. Hardware recording inputs supply audio to the graph; hardware playback outputs receive routed audio. The graph labels below describe the direction at the block, not the names printed on the hardware.</dd>
                <dt><RoutingTypeBadge type="wasapi" /></dt><dd>A bridge to a Windows endpoint. Render sends graph audio to that output; capture imports that recording input; an output mix copy creates a separate source from all shared audio playing on one output. Configure the endpoint and operation in Audio setup → WASAPI.</dd>
                <dt><RoutingTypeBadge type="wasapiApp" /></dt><dd>A new graph source made by copying shared Windows audio from the selected process and its children. The original playback continues independently; Timox controls this copy. Windows playback activity helps identify the process, while the low-level API may remain unknown. The same process can also appear separately as a Timox ASIO client.</dd>
                <dt><RoutingTypeBadge type="process" /></dt><dd>An application using TimoxVasio’s virtual ASIO driver. A Windows network bridge such as PWAR also appears here when its process opens <code>TimoxVasio.dll</code>. Route audio into the client’s inputs to send it across the bridge; route its outputs onward to bring received audio into Windows. The physical ASIO interface remains selected independently.</dd>
                <dt><RoutingTypeBadge type="profile" /></dt><dd>Saved channel capacity for an application. The matching application must open ASIO before those channels can carry audio.</dd>
                <dt><RoutingTypeBadge type="saved" /></dt><dd>Saved connections for an application that is not currently open. They are retained for when its matching channels become available.</dd>
            </dl>
        </section>
        <section><h4>Inputs, outputs and channel counts</h4>
            <p>In the graph, <strong>Outputs</strong> are sources that send audio out of a block; <strong>Inputs</strong> are destinations that receive audio into a block. These labels describe cable direction.</p>
            <p>A hardware recording input such as “Mic 1” therefore appears under <strong>Outputs</strong> in its ASIO block: it supplies recorded audio. A hardware playback output such as “Speakers 1/2” appears under <strong>Inputs</strong>: it receives audio to play. A WASAPI capture bridge likewise supplies Outputs; a WASAPI playback bridge receives Inputs.</p>
            <p>An App block marked <strong>ASIO client connected · no channels allocated</strong> means the application has opened TimoxVasio, but has not created buffers yet. It is a direct client; it has no routable ports until it allocates channels. Once buffers are running, the block reports whether callbacks reach that client. A zero callback count from the selected physical clock means the engine cannot deliver timed blocks yet. Callback and exchanged-frame counts do not prove that the buffers contain audible signal. <strong>Channels allocated, no linked ports shown</strong> means channels exist, but the current connection filter is not showing any of them.</p>
            <p><strong>In 2/8 · Out 1/8 used</strong> means two input channels and one output channel are connected, out of eight available in each direction. Sending one output to several destinations still uses one output channel. Hidden or filtered ports do not change these counts.</p>
            <p>The triangle beside a block name expands or collapses its ports. Square sockets belong to physical ASIO; round sockets belong to other channel types. The badges distinguish a physical ASIO device, a direct Timox ASIO client, a WASAPI device bridge, and a separate process copy. Their labels and icons remain meaningful without relying on color.</p>
        </section>
        <section><h4>Cables and states</h4>
            <dl className="routing-help-cables">
                <dt><i className="routing-help-line active" aria-hidden="true" />Active</dt><dd>The saved connection is present in the running graph. This does not prove that a signal is audible.</dd>
                <dt><i className="routing-help-line flowing" aria-hidden="true" />Signal detected</dt><dd>Small moving dashes show that signal above −90 dBFS is passing on this active, unmuted connection after its gain.</dd>
                <dt><i className="routing-help-line saved" aria-hidden="true" />Saved</dt><dd>The connection is saved but is waiting for its channels to become available.</dd>
                <dt><i className="routing-help-line pending" aria-hidden="true" />Pending</dt><dd>A dashed cable is a draft change. Apply configuration to send it to the engine.</dd>
                <dt><i className="routing-help-line muted" aria-hidden="true" />Muted</dt><dd>A gray dotted cable is muted. The connection stays saved.</dd>
                <dt><i className="routing-help-line selected" aria-hidden="true" />Selected</dt><dd>The cable is emphasized and its ports are outlined. Selection alone opens no properties panel.</dd>
                <dt><span className="routing-help-gain" aria-hidden="true">▲</span>Positive gain</dt><dd>Gain above 0 dB is applied. Open the connection’s context menu to inspect the value.</dd>
            </dl>
            <p>Cable colors can be customized. They do not identify ASIO versus WASAPI. Use the block badge, cable state and moving dashes together.</p>
        </section>
        <section><h4>Actions and layout</h4>
            <p>Right-click the background to add a connection or device. Right-click a block for its actions, or a cable to edit that connection. Detailed settings open in a dialog.</p>
            <p>Auto-arrange moves the blocks; Recenter graph changes the view. Neither changes audio connections. Search and Connection pair only filter what is displayed.</p>
        </section>
    </article>;
}
