// TimoxVasio -> SuperCollider : compresseur multibande + volumes, avec interface.
//
// Démonstration simple :
//   - entrée : 2 canaux ASIO fournis par TimoxVasio (sortie stéréo du DAW) ;
//   - traitement : séparation en 3 bandes, compresseur par bande, volumes ;
//   - sortie : 2 canaux, renvoyés au pilote choisi.
//
// Utilisation : ouvrir ce fichier dans sclang (SuperCollider IDE), placer le
// curseur dans le bloc entre parenthèses, puis lancer avec Ctrl+Entrée (Cmd+Entrée sur macOS).
// Pour tester sans carte ASIO : TIMOX_DEVICE=... (voir ci-dessous).

(
var device = Platform.getEnv("TIMOX_DEVICE");

// Nom du périphérique ASIO. TimoxVasio est le nom du pilote sous Windows.
// Vide ou absent : périphérique par défaut du système (utile pour les tests).
device = if(device.isNil or: { device.isEmpty }, { "TimoxVasio" }, { device });

~noeud = 1000;

s.options.device = device;
s.options.numInputBusChannels = 2;
s.options.numOutputBusChannels = 2;

// Le compresseur. Chaque bande a un seuil (dB), un ratio, et les volumes d'entrée et de sortie.
SynthDef(\timoxCompresseur, {
    |inDb = 0, outDb = 0, xo1 = 200, xo2 = 2000,
     loThr = -18, loRatio = 3,
     midThr = -18, midRatio = 3,
     hiThr = -18, hiRatio = 3,
     bypass = 0|

    var in, lo, mid, hi, wet, sig;
    var compresse = { |x, thrDb, ratio|
        // Compander : au-dessus du seuil, le gain est réduit selon le ratio.
        Compander.ar(x, x, thrDb.dbamp, 1, 1 / ratio, 0.01, 0.2)
    };

    // Entrée depuis TimoxVasio : SoundIn 0 et 1 correspondent aux entrées 1 et 2.
    in = SoundIn.ar([0, 1]) * Lag.kr(inDb.dbamp, 0.05);

    // Séparation en 3 bandes en Linkwitz-Riley 4 (deux filtres d'ordre 2 en cascade),
    // dont la somme reste plate en niveau au point de coupure.
    lo  = LPF.ar(LPF.ar(in, xo1), xo1);
    mid = HPF.ar(HPF.ar(LPF.ar(LPF.ar(in, xo2), xo2), xo1), xo1);
    hi  = HPF.ar(HPF.ar(in, xo2), xo2);

    wet = compresse.(lo, loThr, loRatio)
        + compresse.(mid, midThr, midRatio)
        + compresse.(hi, hiThr, hiRatio);

    sig = Select.ar(bypass, [wet, in]) * Lag.kr(outDb.dbamp, 0.05);

    // Limiteur de sécurité pour éviter les crêtes au-delà de 0 dBFS.
    sig = Limiter.ar(sig, 0.98);

    Out.ar(0, sig);
}).add;

// Interface : une ligne par paramètre, sans effet sur le son si le serveur n'est pas lancé.
s.waitForBoot({
    var win, params, bypassBtn;
    var spec = { |min, max, default, unit| ControlSpec(min, max, \lin, 0.1, default, unit) };

    // [clé du SynthDef, libellé, spécification]
    params = [
        [\inDb,    "Entrée",              spec.(-24, 24, 0, " dB")],
        [\outDb,   "Sortie",              spec.(-24, 24, 0, " dB")],
        [\xo1,     "Coupure bas/médium",  ControlSpec(60, 800, \exp, 1, 200, " Hz")],
        [\xo2,     "Coupure médium/aigu", ControlSpec(800, 8000, \exp, 1, 2000, " Hz")],
        [\loThr,   "Grave : seuil",       spec.(-60, 0, -18, " dB")],
        [\loRatio, "Grave : ratio",       ControlSpec(1, 20, \exp, 0.1, 3, " :1")],
        [\midThr,  "Médium : seuil",      spec.(-60, 0, -18, " dB")],
        [\midRatio,"Médium : ratio",      ControlSpec(1, 20, \exp, 0.1, 3, " :1")],
        [\hiThr,   "Aigu : seuil",        spec.(-60, 0, -18, " dB")],
        [\hiRatio, "Aigu : ratio",        ControlSpec(1, 20, \exp, 0.1, 3, " :1")],
    ];

    // Nœud fixe (1000) : le GUI et le pilotage Node.js (pilotage/pilote.mjs) agissent sur le même synthé.
    s.sendMsg("/s_new", "timoxCompresseur", ~noeud, 0, 0);

    win = Window("TimoxVasio - compresseur multibande", Rect(100, 100, 460, 60 + (params.size + 1) * 30));
    win.view.decorator = FlowLayout(win.view.bounds, 10@10, 8@6);

    params.do({ |p|
        var key = p[0];
        var spc = p[2];
        EZSlider(win, 440@26, p[1], spc, { |ez| s.sendMsg("/n_set", ~noeud, key, ez.value) }, spc.default, labelWidth: 150, numberWidth: 60)
            .setColors(stringBackground: Color.grey(0.15), stringColor: Color.white);
    });

    bypassBtn = Button(win, 440@28).states_([
        ["Compresseur actif", Color.white, Color.green(0.5)],
        ["Bypass (sans traitement)", Color.white, Color.red(0.5)]
    ]).action_({ |b| s.sendMsg("/n_set", ~noeud, \bypass, b.value) });

    win.onClose_({ s.sendMsg("/n_free", ~noeud) });
    win.front;
});
)
