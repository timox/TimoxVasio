import React from 'react';

export default function BufferTimingDiagram({ sampleRate, bufferFrames }) {
    const live = Number.isFinite(sampleRate) && sampleRate > 0 &&
        Number.isFinite(bufferFrames) && bufferFrames > 0;
    const rate = live ? sampleRate : 48000;
    const frames = live ? bufferFrames : 256;
    const sampleUs = 1000000 / rate;
    const blockMs = frames * 1000 / rate;
    const callbacksPerSecond = rate / frames;

    return <section className="section buffer-concepts" aria-label="ASIO sample and buffer timing">
        <div className="section-heading"><div><h2>Samples, buffers, and blocks</h2>
            <p className="hint">{live ? 'Current physical ASIO settings' : 'Example until a physical ASIO driver is active'}</p></div></div>
        <div className="buffer-concept-flow">
            <div className="buffer-concept-step">
                <span className="performance-kicker">1 · Sample rate</span>
                <strong>{rate.toLocaleString('en-US')} samples/s</strong>
                <div className="sample-stream" aria-hidden="true">{Array.from({ length: 8 }, (_, index) =>
                    <span key={index} />)}<b>…</b></div>
                <small>One sample instant every {sampleUs.toFixed(1)} µs, per channel.</small>
            </div>
            <span className="buffer-concept-arrow" aria-hidden="true">→</span>
            <div className="buffer-concept-step buffer-concept-storage">
                <span className="performance-kicker">2 · Alternating ASIO buffers</span>
                <strong>{frames} frames per buffer</strong>
                <div className="asio-buffer-pair" aria-label="Two alternating ASIO buffers">
                    <div><span>Callback n · buffer A</span><i aria-hidden="true">▮ ▮ ▮ ▮ ▮ …</i></div>
                    <div><span>Callback n+1 · buffer B</span><i aria-hidden="true">▮ ▮ ▮ ▮ ▮ …</i></div>
                </div>
                <small>A and B take turns at each callback. Each holds {frames} frames per channel; they are not added together to calculate the block time.</small>
            </div>
            <span className="buffer-concept-arrow" aria-hidden="true">→</span>
            <div className="buffer-concept-step">
                <span className="performance-kicker">3 · Audio block</span>
                <strong>{blockMs.toFixed(2)} ms per block</strong>
                <div className="block-timeline" aria-hidden="true"><span>Block n</span><span>Block n+1</span><span>Block n+2</span></div>
                <small>About {callbacksPerSecond.toFixed(1)} callbacks/s. Each must finish before the next block.</small>
            </div>
        </div>
        <p className="buffer-concept-formula"><strong>{frames} frames ÷ {rate.toLocaleString('en-US')} frames/s = {blockMs.toFixed(2)} ms</strong>
            <span>One callback processes one block in A or B. The other buffer is available to the audio driver.</span></p>
    </section>;
}
