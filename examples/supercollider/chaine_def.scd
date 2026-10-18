// Trois paires stéréo indépendantes : grave, médium, aigu.
// Entrées SC 1-6 -> sorties SC 1-6 dans le même ordre.
~timoxChaineDef = SynthDef(\timoxChaine, {
    |drive = 2, pitch = 0, echoTime = 0.25, echoDecay = 1.5,
     lowBypass = 0, midBypass = 0, highBypass = 0|
    var low, mid, high, saturated, shifted, echoed;

    low = SoundIn.ar([0, 1]);
    mid = SoundIn.ar([2, 3]);
    high = SoundIn.ar([4, 5]);

    saturated = (low * Lag.kr(drive, 0.05)).tanh / Lag.kr(drive, 0.05).tanh;
    shifted = PitchShift.ar(mid, 0.2, Lag.kr(pitch, 0.05).midiratio, 0, 0.01);
    echoed = high + (CombC.ar(high, 1, Lag.kr(echoTime, 0.05),
        Lag.kr(echoDecay, 0.05)) - high) * 0.45;

    Out.ar(0, [
        Limiter.ar(Select.ar(lowBypass, [saturated, low]), 0.98),
        Limiter.ar(Select.ar(midBypass, [shifted, mid]), 0.98),
        Limiter.ar(Select.ar(highBypass, [echoed, high]), 0.98)
    ].flat);
});
