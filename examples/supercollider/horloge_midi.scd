// Horloge MIDI autonome corrigée par la fréquence audio effective.
// À charger après le démarrage de scsynth sur TimoxVasio/SSL.
// Le Tool Renoise configure automatiquement les deux sorties MIDI de la démo.
(
~timoxClockBpm = 120;
~timoxMidiTargets = [];
~timoxClockPpqn = 24;
~timoxClockSamples = List.new;
~timoxClockRejected = 0;
~timoxClockPpm = nil;
~timoxClockLastSampleAt = nil;
~timoxClockOutputs = [];
~timoxClockRunning = false;
~timoxClockTickCount = 0;
~timoxClockTempo = TempoClock.new(~timoxClockBpm / 60);

~timoxClockFresh = {
    ~timoxClockPpm.notNil and: { ~timoxClockLastSampleAt.notNil }
        and: { (SystemClock.seconds - ~timoxClockLastSampleAt) <= 2 };
};

~timoxClockSetBpm = { |bpm|
    if(bpm.isNumber.not or: { bpm < 20 or: { bpm > 300 } }) {
        Error("BPM attendu entre 20 et 300").throw;
    };
    ~timoxClockBpm = bpm;
    ~timoxClockTempo.tempo = (bpm * (1 +
        ((~timoxClockPpm ? 0).clip(-50, 50) / 1000000))) / 60;
    s.sendMsg('/n_set', 2000, 'echoTime', (30 / bpm).clip(0.05, 1));
};

~timoxClockReceiveSample = { |position, rate, at|
    var previous = ~timoxClockSamples.last;
    var elapsed, audioElapsed, points, meanTime, meanSamples, denominator, slope, rawPpm;
    if(position.isNumber and: { rate.isNumber and: { rate > 0 } }) {
        if(previous.notNil) {
            elapsed = at - previous[\time];
            audioElapsed = (position - previous[\position]) / rate;
            if(elapsed > 0 and: { audioElapsed > 0 }
                and: { (elapsed - audioElapsed).abs > 0.03 }) {
                ~timoxClockRejected = ~timoxClockRejected + 1;
                if(~timoxClockRejected >= 3) {
                    ~timoxClockSamples.clear;
                    ~timoxClockPpm = nil;
                    previous = nil;
                };
            };
        };
        if(previous.isNil or: {
            elapsed > 0 and: { audioElapsed > 0 }
                and: { (elapsed - audioElapsed).abs <= 0.03 }
        }) {
            ~timoxClockRejected = 0;
            ~timoxClockSamples.add((time: at, position: position));
            ~timoxClockLastSampleAt = at;
            while { ~timoxClockSamples.size > 2 and: {
                ~timoxClockSamples[1][\time] < (at - 10)
            } } { ~timoxClockSamples.removeAt(0) };
            points = ~timoxClockSamples;
            if(points.size >= 3) {
                meanTime = points.collect { |p| p[\time] }.sum / points.size;
                meanSamples = points.collect { |p| p[\position] }.sum / points.size;
                denominator = points.collect { |p| (p[\time] - meanTime).squared }.sum;
                if(denominator > 0) {
                    slope = points.collect { |p|
                        (p[\time] - meanTime) * (p[\position] - meanSamples)
                    }.sum / denominator;
                    rawPpm = ((slope / rate) - 1) * 1000000;
                    ~timoxClockPpm = (~timoxClockPpm ? 0)
                        + (0.02 * (rawPpm - (~timoxClockPpm ? 0)));
                    ~timoxClockSetBpm.value(~timoxClockBpm);
                };
            };
        };
    };
};

OSCdef(\timoxClockAudioSample).free;
OSCdef(\timoxClockAudioSample, { |message|
    ~timoxClockReceiveSample.value(message[3], message[4], SystemClock.seconds);
}, '/timox/clock/audio', s.addr);

~timoxClockCounterNode = 2001;
SynthDef(\timoxClockCounter, {
    var position = Sweep.kr(0, SampleRate.ir);
    SendReply.kr(Impulse.kr(2), '/timox/clock/audio', [position, SampleRate.ir]);
}).send(s, [\s_new, \timoxClockCounter, ~timoxClockCounterNode, 0, 0]);

~timoxClockStart = {
    if(~timoxClockRunning.not) {
        if(~timoxClockFresh.value.not) {
            "Horloge audio non calibrée : attendre quelques secondes.".postln;
        } {
            MIDIClient.init;
            ~timoxClockOutputs = ~timoxMidiTargets.collect { |target|
                MIDIOut.newByName(target[0], target[1]);
            };
            if(~timoxClockOutputs.isEmpty) {
                "Aucune sortie MIDI configurée dans horloge_midi.scd.".postln;
            } {
                ~timoxClockOutputs.do { |output|
                    output.latency = 0.02;
                    output.start;
                };
                ~timoxClockRunning = true;
                ~timoxClockTickRoutine = Routine({
                    while { ~timoxClockRunning and: { ~timoxClockFresh.value } } {
                        ~timoxClockOutputs.do { |output| output.midiClock };
                        ~timoxClockTickCount = ~timoxClockTickCount + 1;
                        (1 / ~timoxClockPpqn).wait;
                    };
                    if(~timoxClockRunning) {
                        "Référence audio perdue : horloge MIDI arrêtée.".postln;
                        ~timoxClockStop.value;
                    };
                }).play(~timoxClockTempo);
            };
        };
    };
};

~timoxClockStop = {
    if(~timoxClockRunning) {
        ~timoxClockRunning = false;
        if(~timoxClockTickRoutine.notNil) { ~timoxClockTickRoutine.stop };
        ~timoxClockOutputs.do { |output| output.stop };
    };
};

OSCdef(\timoxDemoClockStart).free;
OSCdef(\timoxDemoClockStop).free;
OSCdef(\timoxDemoClockBpm).free;
OSCdef(\timoxDemoClockTargets).free;
OSCdef(\timoxDemoClockAutoTargets).free;
OSCdef(\timoxDemoScStop).free;
~timoxClockIsText = { |value|
    value.isString or: { value.isKindOf(Symbol) }
};
OSCdef(\timoxDemoClockStart, { ~timoxClockStart.value }, '/timox/demo/clock/start');
OSCdef(\timoxDemoClockStop, { ~timoxClockStop.value }, '/timox/demo/clock/stop');
OSCdef(\timoxDemoClockBpm, { |message|
    if(message[1].isNumber) { ~timoxClockSetBpm.value(message[1]) };
}, '/timox/demo/clock/bpm');
OSCdef(\timoxDemoClockTargets, { |message|
    if(message.size == 5 and: { ~timoxClockIsText.value(message[1]) and: {
        ~timoxClockIsText.value(message[2]) and: {
            ~timoxClockIsText.value(message[3]) and: {
                ~timoxClockIsText.value(message[4]) } } }
    }) {
        ~timoxClockStop.value;
        ~timoxMidiTargets = [[message[1].asString, message[2].asString],
            [message[3].asString, message[4].asString]];
        "Deux destinations MIDI configurées depuis le Tool Renoise.".postln;
    };
}, '/timox/demo/clock/targets');
OSCdef(\timoxDemoClockAutoTargets, { |message|
    var names, targets;
    if(message.size == 3 and: { ~timoxClockIsText.value(message[1]) and: {
        ~timoxClockIsText.value(message[2]) } }) {
        names = [message[1].asString, message[2].asString];
        MIDIClient.init;
        MIDIClient.list;
        targets = names.collect { |name|
            MIDIClient.destinations.detect { |endpoint|
                (endpoint.name == name) or: { endpoint.device == name }
            };
        };
        if(targets.any { |target| target.isNil }) {
            "Ports MIDI introuvables dans SuperCollider : ".post;
            names.postln;
            MIDIClient.destinations.postln;
        } {
            ~timoxClockStop.value;
            ~timoxMidiTargets = targets.collect { |endpoint|
                [endpoint.device, endpoint.name]
            };
            "Horloge MIDI configurée : ".post;
            names.postln;
        };
    };
}, '/timox/demo/clock/auto-targets');
OSCdef(\timoxDemoScStop, {
    ~timoxClockStop.value;
    s.quit;
    SystemClock.sched(1, { 0.exit; nil });
}, '/timox/demo/sc/stop');

"Horloge prête. Cliquer Préparer ports MIDI dans le Tool Renoise.".postln;
)
