// SynthDef partagé par le serveur interactif et le rendu WAV hors ligne.
~timoxCompresseurDef = SynthDef(\timoxCompresseur, {
    |inDb = 0, outDb = 0, xo1 = 200, xo2 = 2000,
     loThr = -18, loRatio = 3,
     midThr = -18, midRatio = 3,
     hiThr = -18, hiRatio = 3,
     bypass = 0|

    var in, lo, mid, hi, wet, sig;
    var compresse = { |x, thrDb, ratio|
        Compander.ar(x, x, thrDb.dbamp, 1, 1 / ratio, 0.01, 0.2)
    };

    in = SoundIn.ar([0, 1]) * Lag.kr(inDb.dbamp, 0.05);

    lo  = LPF.ar(LPF.ar(in, xo1), xo1);
    mid = HPF.ar(HPF.ar(LPF.ar(LPF.ar(in, xo2), xo2), xo1), xo1);
    hi  = HPF.ar(HPF.ar(in, xo2), xo2);

    wet = compresse.(lo, loThr, loRatio)
        + compresse.(mid, midThr, midRatio)
        + compresse.(hi, hiThr, hiRatio);

    sig = Select.ar(bypass, [wet, in]) * Lag.kr(outDb.dbamp, 0.05);
    sig = Limiter.ar(sig, 0.98);

    Out.ar(0, sig);
});
