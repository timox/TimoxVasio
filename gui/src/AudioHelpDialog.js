import React, { useEffect, useId, useRef } from 'react';
import { createPortal } from 'react-dom';
import './AudioHelpDialog.css';

const topics = {
    'wasapi-buffer': {
        title: 'Requested buffer duration',
        intro: 'This field requests the native WASAPI shared-mode buffer duration, in milliseconds.',
        sections: [
            ['Choose a value', [
                'Start with 0 to let Windows choose the system default. A nonzero value requests a duration between 0 and 1000 ms; the actual buffer is determined when Windows opens the stream.',
                'A larger buffer can provide more scheduling margin but adds buffering. A smaller request does not guarantee that the driver will use that size or that the complete route will have lower latency.'
            ]],
            ['What this changes', [
                'The value is passed to the Windows shared-mode stream when the bridge configuration is applied. It does not change the graph master clock, its sample rate or its block size.',
                'Requested duration is not measured input-to-output latency. Native buffering, bridge queues, conversion and application processing all contribute to the complete path.'
            ]]
        ]
    },
    'wasapi-mode': {
        title: 'Windows device or process scope',
        intro: 'Choose one of two independent scopes: a Windows device or a process copy.',
        sections: [
            ['Windows device scope', [
                'Capture an input device such as a microphone into the Timox graph. An output mix copy brings the combined shared-mode playback from one Windows output into the graph; it does not isolate one application.',
                'To send Timox audio to Windows, choose an output endpoint as the destination. These input and output operations all use the device scope.'
            ]],
            ['Process copy scope', [
                'A process copy creates a separate graph source from shared-mode audio produced by the selected process and its child processes. It does not depend on the process being tied to a particular Windows endpoint, and original playback continues.',
                'Windows playback activity helps find active processes; it is not a separate bridge scope. ASIO and WASAPI-exclusive output cannot be copied.'
            ]],
            ['What changes', [
                'The choice determines which ports appear in the graph. It is fixed while the bridge has saved connections. Sending a copied signal to the same output as its original may play it twice.'
            ]]
        ]
    },
    'wasapi-process': {
        title: 'Process copy',
        intro: 'A process copy is an independent graph source made from shared Windows audio produced by one selected process.',
        sections: [
            ['Choose a process', [
                'Refresh the active process list after starting playback. Windows playback activity is used to identify eligible processes; the playback session itself is not a separate endpoint or bridge type.',
                'Timox binds the saved copy to the selected process identity, including its creation time, so a later process reusing the same PID is not mistaken for the original.'
            ]],
            ['What gets copied', [
                'The copy includes shared-mode audio from the selected process and its child processes. The app keeps playing to its original Windows destination; Timox does not redirect it or turn it into a Timox ASIO client.',
                'ASIO and WASAPI-exclusive streams are outside this process copy. For an input such as a microphone, choose the Windows device scope instead.'
            ]]
        ]
    },
    'wasapi-endpoint': {
        title: 'Choosing a Windows endpoint',
        intro: 'Choose whether the bridge stays attached to a particular device or follows a Windows default.',
        sections: [
            ['Specific endpoint', [
                'Use a specific endpoint when the connection must stay with the same microphone, speaker or channel group. Changing the Windows default will not move this bridge.',
                'An unavailable endpoint can be saved with Enabled turned off. Its connection stays configured; enable it when the device becomes available.'
            ]],
            ['Windows default role', [
                'Use a default role when you want the bridge to follow the endpoint Windows selects for that role. Console, Multimedia and Communications are separate Windows roles and may resolve to the same device.',
                'The bridge resolves the selected role in its input or output direction. A Windows default change can therefore move the bridge to another endpoint. The resolved endpoint in stream status identifies the device actually opened.'
            ]]
        ]
    },
    'wasapi-status': {
        title: 'Bridge stream status',
        intro: 'Compare the requested configuration with the stream that the engine actually opened.',
        sections: [
            ['Desired state and stream', [
                'Desired shows whether audio is enabled in the bridge configuration. Stream shows runtime state; enabled does not by itself mean that the device opened successfully.',
                'The resolved endpoint and format belong to the opened device. Retrying indicates a scheduled reconnection attempt. Priming fills the bridge buffer before normal transfer.'
            ]],
            ['Queue counters', [
                'Buffered is the current queue fill in graph-rate frames. It changes as audio enters and leaves the queue; it is not a latency measurement.',
                'Rejected counts frames a queue write could not accept. It is cumulative for this bridge generation, not a count of incidents or channels. Rejected frames are not necessarily lost if the writer retries them.',
                'Silenced counts missing graph-rate frames replaced with zero during queue reads. Capture startup while the buffer is priming also contributes. It does not count intentional silence already present in the source signal.',
                'These counters restart with a new bridge generation. Compare their change over time, not just a single nonzero total.'
            ]],
            ['Other observations', [
                'Stream error is the audio operation error. Scheduling error reports a failure to obtain the requested Windows multimedia scheduling support.',
                'Priming timed out records that initial filling exceeded its deadline; it does not permit draining an unfilled capture buffer. Control backlog observations count prolonged drain work exceeding 100 ms, not individual audio dropouts.'
            ]]
        ]
    },
    'analysis-timeline': {
        title: 'Performance history',
        intro: 'Use the timeline to locate when load or transport incidents changed during this Control session.',
        sections: [
            ['Choose what to compare', [
                'Period filters the visible history. Routed application selects the process whose CPU and host callback are plotted. Transport selects one application direction for incident counters.',
                'Callback values above 100% exceed one block period. Process CPU is averaged across all logical processors, so its percentage is a different measurement.'
            ]],
            ['Session data', [
                'Snapshots are collected while Control is open. Missing samples remain gaps; they are not zero load. Stage values appear after Measure stages is enabled in Performance.',
                'Export CSV saves the collected session, including data outside the selected period. Clear session clears the displayed history without resetting engine counters.',
                'Transport bars show changes in cumulative counters; the first sample is a baseline. Channels in one application direction share these counters, so they do not identify a faulty channel.'
            ]]
        ]
    },
    'analysis-correlation': {
        title: 'Stereo correlation',
        intro: 'Correlation compares the signals in the left and right channels of a routed output pair.',
        sections: [
            ['Choose a pair', [
                'Select an active L/R output pair, then start analysis. The engine must be running. Stop analysis stops this measurement, not audio routing.'
            ]],
            ['Read the result', [
                '+1 means the measured signals are fully positively correlated; −1 means they are fully negatively correlated. Values near zero indicate little linear correlation.',
                'Negative correlation can indicate cancellation when channels are combined to mono. Correlation alone does not measure sound quality, latency or CPU load.',
                'No measurable signal is not a correlation value of zero. The small graph shows recent correlation samples; it is separate from the performance timeline.'
            ]]
        ]
    },
    'performance-callback': {
        title: 'Callback timing',
        intro: 'Compare the slowest observed engine callback with the time available for one audio block.',
        sections: [
            ['Read the window', [
                'Time available comes from sample rate and block size. Time left is the block period minus the slowest processing time; a negative margin is shown as Over time.',
                'Delayed starts and processing overruns describe different timing incidents: starting late versus taking longer than the block period. The counts cover the observed window.',
                'A timing incident does not by itself prove an audible dropout. Insufficient data means the window cannot support the usual timing assessment.'
            ]],
            ['Act on the result', [
                'Compare repeatable workloads. Smaller blocks reduce the processing interval but leave less margin; larger blocks add buffering. Neither the margin nor block duration is application-to-speaker latency.'
            ]]
        ]
    },
    'performance-applications': {
        title: 'Application load',
        intro: 'Use these values to identify a busy routed ASIO application.',
        sections: [
            ['Two different percentages', [
                'Process CPU is its share of all logical processors, including work outside audio. Host callback shows the time the application spent in its ASIO callback relative to one audio block.',
                'Enable Measure stages and wait for a complete five-second window to obtain host callback measurements. A dash means no measurement is available.',
                'A busy host can be identified here; TimoxVasio cannot identify an individual VST inside it.'
            ]]
        ]
    },
    'performance-transport': {
        title: 'Application transport counters',
        intro: 'These counters describe the shared audio transport for each routed application direction.',
        sections: [
            ['Read the counters', [
                'An underrun means the reader did not have the expected audio available. An overrun means the writer could not retain all pending audio in the transport.',
                'Channels in the same application direction share counters. A repeated number on several channels does not mean several independent incidents.',
                'Only directions used by active routes are shown. A dash means no meter event has arrived yet. Compare counter changes during a test; a nonzero total can describe an earlier incident.'
            ]]
        ]
    },
    'performance-stages': {
        title: 'Stage measurements',
        intro: 'Enable Measure stages to observe where time is spent along the ASIO application path.',
        sections: [
            ['Read each stage', [
                'Host callback measures the application work performed during its ASIO callback. Virtual ring write measures publication of audio into the shared transport.',
                'Ring wait measures the interval from publication to reading the same complete block. It is waiting time in the transport, not acoustic latency; discarded blocks are reported separately.',
                'Client read, Routing and Physical output copy measure the corresponding engine processing stages. Physical output copy is absent when using the software clock.'
            ]],
            ['Read the summary', [
                'Mean, Worst and sample count refer to the last complete five-second window. A dash means no samples, not zero processing time.',
                'Stage worst values need not come from the same block. Adding them does not measure end-to-end latency. Driver, device and application buffering may add delays beyond these observations.'
            ]]
        ]
    },
    'api-swagger': {
        title: 'HTTP API reference',
        intro: 'Swagger presents the OpenAPI specification bundled with this application.',
        sections: [
            ['Browse the contract', [
                'Use the reference to inspect HTTP endpoints, request fields, response schemas and error formats. The documentation is bundled locally; reading it does not change audio configuration.',
                'Executing a request is different from reading the reference. Commands and write requests target the real engine and may change its state; review the operation and payload before sending.'
            ]]
        ]
    },
    'api-websocket': {
        title: 'WebSocket commands and events',
        intro: 'Commands request an action; events report changes or measurements from the engine.',
        sections: [
            ['Choose the message', [
                'The local connection uses the vasio.api.v1 subprotocol. A command carries an ID, a command name and, where required, a payload.',
                'configuration.apply submits an audio and routing configuration. engine.start and engine.stop control the real engine; stopping is rejected while an ASIO client is connected.',
                'Events such as engine.status or audio.meter describe observed state. Viewing them does not send a new command.'
            ]]
        ]
    },
    'api-console': {
        title: 'Sending an API request',
        intro: 'The console sends requests to the live local engine, not a simulation.',
        sections: [
            ['Prepare the request', [
                'Select HTTP or WebSocket, choose a preset, then check the complete JSON payload. A preset fills the editor; Run sends it.',
                'Read state with GET /api/v1/state when you need a current snapshot. Commands such as configuration.apply and HTTP write requests can change configuration or operation.'
            ]],
            ['Check the outcome', [
                'Response or error shows the result of the request. Recent events provide subsequent engine observations; an event is not a replacement for checking the request result.',
                'The console asks for confirmation for engine.stop and configuration.apply. Do not assume every other write operation is read-only or has a confirmation step.'
            ]]
        ]
    },
    clock: {
        title: 'Audio clock',
        intro: 'The master clock sets the pace of audio processing. It is not MIDI tempo.',
        sections: [
            ['Choose a master', [
                'ASIO: choose your physical interface when using its inputs or outputs. Its callbacks pace the graph.',
                'Software: use this for application routing or WASAPI devices without a physical ASIO interface. Timing depends on Windows scheduling.',
                'WASAPI is not a selectable master in this version. Each device follows the graph through buffering and rate adaptation.'
            ]],
            ['Rate and block size', [
                'Sample rate is the number of samples per second. Block size is the number processed at once. At 48,000 Hz, 256 frames represent 5.33 ms per block; this is not the total input-to-output latency.',
                'Smaller blocks leave less time for processing. Larger blocks give more margin but add buffering. Neither clock choice guarantees lower total latency.'
            ]],
            ['Changing the clock', [
                'Applying a clock change briefly interrupts audio. Software clock keeps physical ASIO routes saved but inactive; selecting a compatible ASIO driver makes those hardware ports usable again.',
                'WASAPI devices retain their own clocks, even at the same nominal sample rate. Their bridges correct small clock differences. MIDI Clock generation is not implemented.'
            ]]
        ]
    },
    wasapi: {
        title: 'Windows audio connections',
        intro: 'Timox routes audio after it joins the graph. A direct client opens the Timox ASIO driver; Timox can also add a separate copy of shared Windows audio. Copying audio does not make an app an ASIO client.',
        sections: [
            ['Direct client or process copy?', [
                'Direct ASIO membership means the application opened TimoxVasio.dll. Timox can identify the driver client and route the buffers it sends to the driver; this does not guarantee that every buffer contains audible signal.',
                'A process copy is created by Timox. It copies shared Windows audio from the selected process and its children into a new graph source. This copy does not connect the process to Timox ASIO or redirect its original Windows playback. If the same process also opens Timox ASIO, it appears as a separate source.',
                'An output-mix copy takes the combined shared audio from one Windows output. It does not identify the applications in that mix. A WASAPI device bridge opens an existing Windows device for Timox; it is not an endpoint offered to applications.'
            ]],
            ['Create a process copy', [
                'Start audio in an application such as Firefox, refresh active processes, create a process copy and apply the configuration. Timox then exposes a new graph source.',
                'This copy covers shared-mode audio from the selected process and its children. It is not tied to one Windows output device and does not include ASIO or WASAPI-exclusive audio.',
                'The application’s original output remains unchanged. If the same process is also a Timox ASIO client, these are separate sources. Routing both copies to one output can play the sound twice.'
            ]],
            ['Copy a Windows output mix', [
                'An output mix copy creates one source from all shared audio playing through a selected Windows output. Use it when a combined mix is what you want; it does not identify or separate the applications in that mix.'
            ]],
            ['Available and offline devices', [
                'Save a bridge, then apply the configuration to use it in Routing. An unavailable device can be saved with audio disabled. Enable it when it becomes available.',
                'A device bridge uses the existing Windows driver; it does not install a new Windows audio device. A recording input sends its audio into the graph; a playback bridge sends graph audio to Windows.'
            ]],
            ['Different sample rates', [
                'With a 44.1 kHz device and a 48 kHz graph, input audio is converted from 44.1 to 48 kHz; output audio is converted from 48 to 44.1 kHz. The device keeps the mix format provided by Windows.',
                'Conversion adds CPU work and buffering delay. Upsampling does not restore missing information, and converted audio is not bit-perfect. Matching rates still requires correction for independent device clocks.'
            ]],
            ['Limits and practical checks', [
                'More channels, devices and conversions increase CPU work. Small buffers may produce dropouts if processing or Windows scheduling is late.',
                'Drift correction handles small clock differences, not unbounded delays or disconnections. Configurations exceeding the audio queue capacity are rejected.',
                'The converter is tested at 44.1 ↔ 48 kHz. Long-running stability with your devices and quality at other rate ratios still require measurement. Avoid unnecessary rate changes and check the complete route under load.',
                'Routing an output mix copy back to the same Windows output is not allowed.'
            ]]
        ]
    },
    asio: {
        title: 'ASIO devices and profiles',
        intro: 'Applications select TimoxVasio as their virtual ASIO driver.',
        sections: [
            ['Virtual and physical drivers', [
                'TimoxVasio.dll remains an autonomous virtual ASIO driver. A physical ASIO driver supplies hardware inputs and outputs and can provide the master audio clock.',
                'Choose the physical driver in ASIO. Set its sample rate and block size in Clock. Software clock lets application routing run without a physical ASIO card.'
            ]],
            ['Application channel profiles', [
                'A profile defines the channel capacity offered to an application. Assign a profile from the process block in Routing. Changes take effect when the application opens ASIO again.',
                'Open channels are not automatically connected. Use Show all ports on the process when you need to see ports without routes.'
            ]]
        ]
    },
    performance: {
        title: 'Reading performance',
        intro: 'Use these measurements to check whether audio processing finishes in time.',
        sections: [
            ['Process CPU and audio callback', [
                'Process CPU is its share of all logical processors, including work outside audio. The callback figure shows how much of one block its audio processing used.',
                'A busy host can be identified here; TimoxVasio cannot identify an individual VST inside it. An overrun means processing exceeded the time available for the block.'
            ]],
            ['Processing interval is not total latency', [
                'At 48,000 Hz, a 256-frame block lasts 5.33 ms. The full application-to-speaker path also includes driver, device, bridge and application buffering. End-to-end latency is not measured here.',
                'Small blocks reduce the processing interval but leave less time to finish. Compare the actual route under load and watch for overruns and dropouts.'
            ]],
            ['WASAPI conversion', [
                'Rate conversion and additional devices add CPU work and buffering. Equal nominal rates do not synchronize independent device clocks.',
                'Tests at 44.1 ↔ 48 kHz do not qualify every device, long-running hardware stability or conversion quality at extreme rate ratios.'
            ]]
        ]
    }
};

topics.analysis = {
    title: 'Analysis guide',
    intro: 'Choose a view for the question you are investigating: timing history, transport incidents, channel use or stereo correlation.',
    sections: [
        ...topics['analysis-timeline'].sections,
        ['Channel states', [
            'Configured means a saved route targets that channel. Open means the application requested the ASIO channel. Routed now means an active route connects an open channel; it does not prove that audio is present.',
            'A per-channel frequency spectrum is not collected in this version.'
        ]],
        ...topics['analysis-correlation'].sections
    ]
};
topics['analysis-transport'] = {
    title: 'Transport incident history',
    intro: 'Select an application transport direction to see changes in its underrun and overrun counters.',
    sections: [
        ['Choose a period and transport', [
            'Each bar represents a collected interval. The first sample is a baseline, not an interval of observed zero incidents. The period filter selects the visible history.',
            'Channels in one application direction share counters. These bars cannot identify a faulty channel. Compare changes around the time of an audio problem.'
        ]],
        ['Interpreting gaps and totals', [
            'History is collected while Control is open. Missing samples are not evidence that transport was healthy. Totals cover observed counter increases in the selected period.',
            'Clear session clears this local history; it does not reset the engine counters.'
        ]]
    ]
};
topics['analysis-channels'] = {
    title: 'Configured and active channels',
    intro: 'These counts distinguish saved routing from the channels an application actually opened.',
    sections: [topics.analysis.sections.find(([heading]) => heading === 'Channel states')]
};
topics.api = {
    title: 'API guide',
    intro: 'The reference describes the contract. The console sends real requests. Events report observations from the engine.',
    sections: [
        ...topics['api-swagger'].sections,
        ...topics['api-websocket'].sections,
        ...topics['api-console'].sections
    ]
};
topics.performance.sections.push(...topics['performance-stages'].sections);

const contentFor = topic => Object.prototype.hasOwnProperty.call(topics, topic) ? topics[topic] : topics.clock;

export function AudioHelpContent({ topic }) {
    const content = contentFor(topic);
    return <div className="audio-help-content">
        {topic === 'wasapi' && <AudioAdhesionDiagram />}
        <p className="audio-help-intro">{content.intro}</p>
        {content.sections.map(([heading, paragraphs]) => <section key={heading}>
            <h3>{heading}</h3>
            {paragraphs.map(paragraph => <p key={paragraph}>{paragraph}</p>)}
        </section>)}
    </div>;
}

function AudioAdhesionDiagram() {
    return <figure className="audio-adhesion-diagram">
        <svg viewBox="0 0 760 390" role="img" aria-labelledby="audio-adhesion-title audio-adhesion-desc">
            <title id="audio-adhesion-title">How audio joins the Timox graph</title>
            <desc id="audio-adhesion-desc">ASIO apps join through the Timox driver. Shared audio rendered by a Windows process is copied into a separate graph source while original playback continues. An output mix copy brings a whole mixed output into the graph.</desc>
            <defs>
                <marker id="adhesion-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" /></marker>
                <marker id="adhesion-copy-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M 0 0 L 10 5 L 0 10 z" /></marker>
            </defs>
            <text className="adhesion-section-title" x="16" y="23">Direct ASIO client · app opens the Timox driver</text>
            <rect className="adhesion-node" x="16" y="38" width="170" height="58" rx="9" />
            <text className="adhesion-node-title" x="101" y="63" textAnchor="middle">ASIO application</text>
            <text className="adhesion-node-subtitle" x="101" y="82" textAnchor="middle">client identity + audio</text>
            <line className="adhesion-link" x1="190" y1="67" x2="270" y2="67" markerStart="url(#adhesion-arrow)" markerEnd="url(#adhesion-arrow)" />
            <rect className="adhesion-node adhesion-driver" x="274" y="38" width="190" height="58" rx="9" />
            <text className="adhesion-node-title" x="369" y="63" textAnchor="middle">TimoxVasio driver</text>
            <text className="adhesion-node-subtitle" x="369" y="82" textAnchor="middle">explicit client connection</text>
            <line className="adhesion-link" x1="468" y1="67" x2="548" y2="67" markerStart="url(#adhesion-arrow)" markerEnd="url(#adhesion-arrow)" />
            <rect className="adhesion-node adhesion-graph" x="552" y="38" width="192" height="58" rx="9" />
            <text className="adhesion-node-title" x="648" y="73" textAnchor="middle">Timox graph</text>

            <text className="adhesion-section-title" x="16" y="128">Windows shared audio · Timox makes a separate copy</text>
            <rect className="adhesion-node" x="16" y="143" width="150" height="58" rx="9" />
            <text className="adhesion-node-title" x="91" y="168" textAnchor="middle">Firefox</text>
            <text className="adhesion-node-subtitle" x="91" y="187" textAnchor="middle">original audio</text>
            <line className="adhesion-original-link" x1="170" y1="172" x2="236" y2="172" markerEnd="url(#adhesion-arrow)" />
            <rect className="adhesion-node adhesion-windows" x="240" y="143" width="210" height="58" rx="9" />
            <text className="adhesion-node-title" x="345" y="168" textAnchor="middle">Windows shared audio</text>
            <text className="adhesion-node-subtitle" x="345" y="187" textAnchor="middle">original playback continues</text>
            <line className="adhesion-original-link" x1="454" y1="172" x2="520" y2="172" markerEnd="url(#adhesion-arrow)" />
            <rect className="adhesion-node adhesion-device" x="524" y="143" width="220" height="58" rx="9" />
            <text className="adhesion-node-title" x="634" y="168" textAnchor="middle">Jabra or other output</text>
            <text className="adhesion-node-subtitle" x="634" y="187" textAnchor="middle">unchanged original route</text>

            <path className="adhesion-copy-link" d="M345 205 V230 H230" markerEnd="url(#adhesion-copy-arrow)" />
            <rect className="adhesion-node adhesion-copy" x="16" y="240" width="214" height="58" rx="9" />
            <text className="adhesion-node-title" x="123" y="265" textAnchor="middle">Process copy</text>
            <text className="adhesion-node-subtitle" x="123" y="284" textAnchor="middle">process and child processes</text>
            <line className="adhesion-copy-link" x1="234" y1="269" x2="314" y2="269" markerEnd="url(#adhesion-copy-arrow)" />
            <rect className="adhesion-node adhesion-source" x="318" y="240" width="178" height="58" rx="9" />
            <text className="adhesion-node-title" x="407" y="265" textAnchor="middle">New graph source</text>
            <text className="adhesion-node-subtitle" x="407" y="284" textAnchor="middle">Timox routes this copy only</text>
            <line className="adhesion-copy-link" x1="500" y1="269" x2="548" y2="269" markerEnd="url(#adhesion-copy-arrow)" />
            <rect className="adhesion-node adhesion-graph" x="552" y="240" width="192" height="58" rx="9" />
            <text className="adhesion-node-title" x="648" y="275" textAnchor="middle">Timox graph</text>

            <text className="adhesion-section-title" x="16" y="331">For an aggregate source · copy the endpoint mix</text>
            <rect className="adhesion-node adhesion-windows" x="16" y="344" width="255" height="38" rx="8" />
            <text className="adhesion-node-title" x="143" y="368" textAnchor="middle">Windows output · apps mixed</text>
            <line className="adhesion-copy-link" x1="275" y1="363" x2="374" y2="363" markerEnd="url(#adhesion-copy-arrow)" />
            <rect className="adhesion-node adhesion-copy" x="378" y="344" width="366" height="38" rx="8" />
            <text className="adhesion-node-title" x="561" y="368" textAnchor="middle">Output mix copy · one combined source</text>
        </svg>
        <figcaption>ASIO is a direct client connection. Timox makes a separate process copy for a new graph source; this does not create an ASIO connection and leaves original playback unchanged. The same process can appear as a separate ASIO source. An output mix copy brings the whole output mix into the graph.</figcaption>
    </figure>;
}

export default function AudioHelpDialog({ topic, onClose }) {
    const content = contentFor(topic);
    const titleId = useId();
    const dialogRef = useRef(null);
    const closeButton = useRef(null);
    const close = useRef(onClose);
    close.current = onClose;
    useEffect(() => {
        const previous = document.activeElement;
        closeButton.current?.focus();
        const keydown = event => {
            if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); close.current(); return; }
            if (event.key !== 'Tab') return;
            const items = [...dialogRef.current.querySelectorAll('button:not(:disabled), a[href], [tabindex="0"]')];
            const first = items[0], last = items[items.length - 1];
            if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
            else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
        };
        const keepFocus = event => {
            if (!dialogRef.current?.contains(event.target)) closeButton.current?.focus();
        };
        document.addEventListener('keydown', keydown, true);
        document.addEventListener('focusin', keepFocus);
        return () => {
            document.removeEventListener('keydown', keydown, true);
            document.removeEventListener('focusin', keepFocus);
            if (previous?.isConnected) previous.focus();
        };
    }, []);
    return createPortal(<div className="audio-help-backdrop" onMouseDown={event => {
        if (event.target === event.currentTarget) onClose();
    }}>
        <section className="audio-help-dialog" ref={dialogRef} role="dialog" aria-modal="true" aria-labelledby={titleId}>
            <header className="audio-help-header">
                <h2 id={titleId}>{content.title}</h2>
                <button type="button" ref={closeButton} onClick={onClose}>Close help</button>
            </header>
            <div className="audio-help-body" tabIndex={0} role="region" aria-label={`${content.title} help`}>
                <AudioHelpContent topic={topic} />
            </div>
        </section>
    </div>, document.body);
}
