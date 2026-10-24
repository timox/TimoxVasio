import React, { useEffect, useId, useRef } from 'react';
import { createPortal } from 'react-dom';
import './AudioHelpDialog.css';

const topics = {
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
        title: 'Windows audio devices',
        intro: 'A WASAPI bridge connects an existing Windows input or output to the routing graph.',
        sections: [
            ['Available and offline devices', [
                'Save a bridge, then apply the configuration to use it in Routing. An unavailable device can be saved with audio disabled. Enable it when it becomes available.',
                'A bridge uses the existing Windows driver; it does not install a new Windows audio device. Output loopback captures the sound playing through that output.'
            ]],
            ['Different sample rates', [
                'With a 44.1 kHz device and a 48 kHz graph, input audio is converted from 44.1 to 48 kHz; output audio is converted from 48 to 44.1 kHz. The device keeps the mix format provided by Windows.',
                'Conversion adds CPU work and buffering delay. Upsampling does not restore missing information, and converted audio is not bit-perfect. Matching rates still requires correction for independent device clocks.'
            ]],
            ['Limits and practical checks', [
                'More channels, devices and conversions increase CPU work. Small buffers may produce dropouts if processing or Windows scheduling is late.',
                'Drift correction handles small clock differences, not unbounded delays or disconnections. Configurations exceeding the audio queue capacity are rejected.',
                'The converter is tested at 44.1 ↔ 48 kHz. Long-running stability with your devices and quality at other rate ratios still require measurement. Avoid unnecessary rate changes and check the complete route under load.',
                'Connecting endpoint loopback back to the same Windows output is not allowed.'
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

export default function AudioHelpDialog({ topic, onClose }) {
    const content = Object.prototype.hasOwnProperty.call(topics, topic) ? topics[topic] : topics.clock;
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
                <p className="audio-help-intro">{content.intro}</p>
                {content.sections.map(([heading, paragraphs]) => <section key={heading}>
                    <h3>{heading}</h3>
                    {paragraphs.map(paragraph => <p key={paragraph}>{paragraph}</p>)}
                </section>)}
            </div>
        </section>
    </div>, document.body);
}
