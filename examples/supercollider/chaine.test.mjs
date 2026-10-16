import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const directory = path.dirname(fileURLToPath(import.meta.url));
const sclang = process.env.SCLANG_PATH ?? 'C:\\Program Files\\SuperCollider-3.14.1\\sclang.exe';

function runSclang(args, timeoutMs = 60000) {
  return new Promise((resolve, reject) => {
    const child = spawn(sclang, ['-a', '--include-path', path.join(path.dirname(sclang), 'SCClassLibrary'), ...args], { windowsHide: true });
    let log = '';
    const timeout = setTimeout(() => { child.kill(); reject(new Error(`sclang trop long : ${log}`)); }, timeoutMs);
    child.stdout.on('data', chunk => { log += chunk; });
    child.stderr.on('data', chunk => { log += chunk; });
    child.on('error', error => { clearTimeout(timeout); reject(error); });
    child.on('close', code => { clearTimeout(timeout); resolve({ code, log }); });
  });
}

function writeSixChannelWav(file) {
  const rate = 48000;
  const frames = rate * 2;
  const bytesPerFrame = 12;
  const samples = Buffer.alloc(frames * bytesPerFrame);
  for (let i = 0; i < frames; i++) {
    for (let channel = 0; channel < 6; channel++) {
      const sample = Math.round(0.15 * Math.sin(2 * Math.PI * (110 + channel * 110) * i / rate) * 32767);
      samples.writeInt16LE(sample, i * bytesPerFrame + channel * 2);
    }
  }
  const header = Buffer.alloc(44);
  header.write('RIFF', 0); header.writeUInt32LE(36 + samples.length, 4);
  header.write('WAVEfmt ', 8); header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20); header.writeUInt16LE(6, 22);
  header.writeUInt32LE(rate, 24); header.writeUInt32LE(rate * bytesPerFrame, 28);
  header.writeUInt16LE(bytesPerFrame, 32); header.writeUInt16LE(16, 34);
  header.write('data', 36); header.writeUInt32LE(samples.length, 40);
  writeFileSync(file, Buffer.concat([header, samples]));
}

test('la chaîne SC rend six canaux non silencieux', { skip: !existsSync(sclang) }, async () => {
  const temp = mkdtempSync(path.join(os.tmpdir(), 'timox-chaine-'));
  const input = path.join(temp, 'input.wav');
  const output = path.join(temp, 'output.wav');
  try {
    writeSixChannelWav(input);
    const result = await runSclang([path.join(directory, 'render_chaine_wav.scd'), input, output]);
    assert.equal(result.code, 0, result.log);
    const wav = readFileSync(output);
    assert.equal(wav.toString('ascii', 0, 4), 'RIFF');
    assert.equal(wav.readUInt16LE(22), 6);
    const data = wav.indexOf(Buffer.from('data')) + 8;
    assert.ok(data >= 8);
    const energy = Array(6).fill(0);
    for (let frame = 24000; frame < 48000; frame++) {
      for (let channel = 0; channel < 6; channel++) {
        const value = wav.readInt16LE(data + (frame * 6 + channel) * 2);
        energy[channel] += value * value;
      }
    }
    energy.forEach((value, channel) => assert.ok(value > 1000000, `canal ${channel + 1} silencieux`));
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
});

test('les scripts interactifs SC se compilent', { skip: !existsSync(sclang) }, async () => {
  const temp = mkdtempSync(path.join(os.tmpdir(), 'timox-sc-compile-'));
  const script = path.join(temp, 'compile.scd');
  try {
    const files = ['timox_chaine.scd', 'horloge_midi.scd'];
    const statements = files.map(file => `File.readAllString(${JSON.stringify(path.join(directory, file))}).compile;`);
    writeFileSync(script, `${statements.join('\n')}\n"SC_COMPILE_PASS".postln;\n0.exit;\n`);
    const result = await runSclang([script], 15000);
    assert.equal(result.code, 0, result.log);
    assert.match(result.log, /SC_COMPILE_PASS/);
    assert.doesNotMatch(result.log, /ERROR:|Syntax Error/);
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
});

test('la correction MIDI suit une fréquence audio plus lente', { skip: !existsSync(sclang) }, async () => {
  const temp = mkdtempSync(path.join(os.tmpdir(), 'timox-clock-'));
  const script = path.join(temp, 'clock.scd');
  try {
    const clock = path.join(directory, 'horloge_midi.scd');
    writeFileSync(script, `
thisProcess.interpreter.executeFile(${JSON.stringify(clock)});
~timoxClockSetBpm = { |bpm| ~timoxClockBpm = bpm };
20.do { |i| ~timoxClockReceiveSample.value(i * 24000, 48000, i * 0.500016); };
if(~timoxClockPpm.isNil or: { ~timoxClockPpm >= 0 or: { ~timoxClockPpm < -50 } }) {
    Error("Correction de fréquence incorrecte").throw;
};
"SC_CLOCK_PASS".postln;
0.exit;
`);
    const result = await runSclang([script], 15000);
    assert.equal(result.code, 0, result.log);
    assert.match(result.log, /SC_CLOCK_PASS/);
    assert.doesNotMatch(result.log, /ERROR:/);
  } finally {
    rmSync(temp, { recursive: true, force: true });
  }
});
