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
var device = "TIMOX_DEVICE".getenv;
var defEnv;
var noeud = 1000;

// Nom du périphérique ASIO. TimoxVasio est le nom du pilote sous Windows.
// Si la variable est absente ou vide, utiliser TimoxVasio.
device = if(device.isNil or: { device.isEmpty }, { "TimoxVasio" }, { device });

s.options.device = device;
s.options.numInputBusChannels = 2;
s.options.numOutputBusChannels = 2;

// Charger exactement le SynthDef utilisé pour le rendu WAV hors ligne.
defEnv = Environment.make({
    thisProcess.interpreter.executeFile(PathName(thisProcess.nowExecutingPath).pathOnly +/+ "compresseur_def.scd");
});

// Interface compacte : cinq rangées de deux réglages.
s.waitForBoot({
    var win, params, bypassBtn;
    var spec = { |min, max, default, unit| ControlSpec(min, max, \lin, 0.1, default, unit) };

    // [clé du SynthDef, libellé, spécification]
    params = [
        [\inDb,    "Entrée",              spec.(-24, 24, 0, " dB")],
        [\outDb,   "Sortie",              spec.(-24, 24, 0, " dB")],
        [\xo1,     "Bas/médium",          ControlSpec(60, 800, \exp, 1, 200, " Hz")],
        [\xo2,     "Médium/aigu",         ControlSpec(800, 8000, \exp, 1, 2000, " Hz")],
        [\loThr,   "Grave seuil",         spec.(-60, 0, -18, " dB")],
        [\loRatio, "Grave ratio",         ControlSpec(1, 20, \exp, 0.1, 3, " :1")],
        [\midThr,  "Médium seuil",        spec.(-60, 0, -18, " dB")],
        [\midRatio,"Médium ratio",        ControlSpec(1, 20, \exp, 0.1, 3, " :1")],
        [\hiThr,   "Aigu seuil",          spec.(-60, 0, -18, " dB")],
        [\hiRatio, "Aigu ratio",          ControlSpec(1, 20, \exp, 0.1, 3, " :1")],
    ];

    // Créer le nœud uniquement après le chargement asynchrone du SynthDef.
    defEnv[\timoxCompresseurDef].send(s, [\s_new, \timoxCompresseur, noeud, 0, 0]);

    win = Window("TimoxVasio - compresseur multibande", Rect(100, 100, 430, 200));

    params.do({ |p, i|
        var key = p[0];
        var spc = p[2];
        var bounds = Rect(10 + ((i % 2) * 210), 10 + ((i / 2).floor * 29), 200, 25);
        EZSlider(win, bounds, p[1], spc, { |ez| s.sendMsg("/n_set", noeud, key, ez.value) }, spc.default, labelWidth: 86, numberWidth: 52)
            .setColors(stringBackground: Color.grey(0.15), stringColor: Color.white);
    });

    bypassBtn = Button(win, Rect(10, 164, 410, 28)).states_([
        ["Compresseur actif", Color.white, Color.green(0.5)],
        ["Bypass (sans compression)", Color.white, Color.red(0.5)]
    ]).action_({ |b| s.sendMsg("/n_set", noeud, \bypass, b.value) });

    win.onClose_({ s.sendMsg("/n_free", noeud) });
    win.front;
});
)
