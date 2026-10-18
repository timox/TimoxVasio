// Renoise (3 paires) -> SuperCollider -> Ableton (3 paires).
// Ouvrir dans SuperCollider, sélectionner tout le bloc, Ctrl+Entrée.
(
var device = "TIMOX_DEVICE".getenv;
var base = PathName(thisProcess.nowExecutingPath).pathOnly;
var defEnv;
var node = 2000;

if(s.serverRunning) {
    Error("Arrêter le serveur SuperCollider avant cette démo (Ctrl+.). Elle requiert six entrées et six sorties.").throw;
};

device = if(device.isNil or: { device.isEmpty }, { "TimoxVasio" }, { device });
s.options.device = device;
s.options.numInputBusChannels = 6;
s.options.numOutputBusChannels = 6;

defEnv = Environment.make({
    thisProcess.interpreter.executeFile(base +/+ "chaine_def.scd");
});

s.waitForBoot({
    defEnv[\timoxChaineDef].send(s, [\s_new, \timoxChaine, node, 0, 0]);
    thisProcess.interpreter.executeFile(base +/+ "horloge_midi.scd");
    "Audio : 3 paires d'entrée vers 3 paires de sortie.".postln;
    "Réglages : s.sendMsg('/n_set', 2000, 'pitch', 7) ou 'drive', 'echoTime'.".postln;
    "MIDI : configurer ~timoxMidiTargets puis ~timoxClockStart.value.".postln;
});
)
