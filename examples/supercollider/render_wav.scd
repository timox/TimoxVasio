// Usage : sclang render_wav.scd entree.wav sortie.wav
// Rendu hors ligne du même compresseur que timox_compresseur.scd.
(
var inputPath = thisProcess.argv[0];
var outputPath = thisProcess.argv[1];
var inputFile, duration, sampleRate, server, score, defEnv;

if(inputPath.isNil or: { outputPath.isNil }, {
    "Usage : sclang render_wav.scd entree.wav sortie.wav".postln;
    1.exit;
});

inputFile = SoundFile.openRead(inputPath);
if(inputFile.isNil, {
    ("Impossible d'ouvrir le WAV : " ++ inputPath).postln;
    1.exit;
});
if(inputFile.numChannels != 2, {
    "Le WAV d'entrée doit être stéréo.".postln;
    inputFile.close;
    1.exit;
});
duration = inputFile.duration;
sampleRate = inputFile.sampleRate;
inputFile.close;

defEnv = Environment.make({
    thisProcess.interpreter.executeFile(PathName(thisProcess.nowExecutingPath).pathOnly +/+ "compresseur_def.scd");
});
server = Server(\timoxRendu, options: ServerOptions.new.numInputBusChannels_(2).numOutputBusChannels_(2));
score = Score([
    [0.0, [\d_recv, defEnv[\timoxCompresseurDef].asBytes]],
    [0.0, [\s_new, \timoxCompresseur, 1000, 0, 0]],
    [duration, [\n_free, 1000]]
]);

score.recordNRT(
    outputFilePath: outputPath,
    inputFilePath: inputPath,
    sampleRate: sampleRate,
    headerFormat: "WAV",
    sampleFormat: "int16",
    options: server.options,
    duration: duration,
    action: {
        server.remove;
        ("WAV traité : " ++ outputPath).postln;
        0.exit;
    }
);
)
