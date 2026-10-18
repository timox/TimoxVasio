// Usage : sclang render_chaine_wav.scd six-entrees.wav six-sorties.wav
// WAV à six canaux dans l'ordre grave L/R, médium L/R, aigu L/R.
(
var inputPath = thisProcess.argv[0];
var outputPath = thisProcess.argv[1];
var inputFile, duration, sampleRate, server, score, defEnv;

if(inputPath.isNil or: { outputPath.isNil }, {
    "Usage : sclang render_chaine_wav.scd six-entrees.wav six-sorties.wav".postln;
    1.exit;
});
inputFile = SoundFile.openRead(inputPath);
if(inputFile.isNil, {
    ("Impossible d'ouvrir le WAV : " ++ inputPath).postln;
    1.exit;
});
if(inputFile.numChannels != 6, {
    "Le WAV d'entrée doit avoir six canaux.".postln;
    inputFile.close;
    1.exit;
});
duration = inputFile.duration;
sampleRate = inputFile.sampleRate;
inputFile.close;

defEnv = Environment.make({
    thisProcess.interpreter.executeFile(PathName(thisProcess.nowExecutingPath).pathOnly +/+ "chaine_def.scd");
});
server = Server(\timoxChaineRendu,
    options: ServerOptions.new.numInputBusChannels_(6).numOutputBusChannels_(6));
score = Score([
    [0.0, [\d_recv, defEnv[\timoxChaineDef].asBytes]],
    [0.0, [\s_new, \timoxChaine, 1000, 0, 0]],
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
