import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const script = fileURLToPath(new URL('./render_wav.scd', import.meta.url));
const sclang = process.env.SCLANG_PATH ?? 'C:\\Program Files\\SuperCollider-3.14.1\\sclang.exe';

function writeWav(file, amplitude) {
  const sampleRate = 44100;
  const frames = sampleRate * 2;
  const data = Buffer.alloc(frames * 2 * 2);
  for (let i = 0; i < frames; i++) {
    const value = Math.round(Math.sin(2 * Math.PI * 1000 * i / sampleRate) * amplitude * 32767);
    data.writeInt16LE(value, i * 4);
    data.writeInt16LE(value, i * 4 + 2);
  }
  const wav = Buffer.alloc(44);
  wav.write('RIFF', 0); wav.writeUInt32LE(36 + data.length, 4); wav.write('WAVEfmt ', 8);
  wav.writeUInt32LE(16, 16); wav.writeUInt16LE(1, 20); wav.writeUInt16LE(2, 22);
  wav.writeUInt32LE(sampleRate, 24); wav.writeUInt32LE(sampleRate * 4, 28);
  wav.writeUInt16LE(4, 32); wav.writeUInt16LE(16, 34);
  wav.write('data', 36); wav.writeUInt32LE(data.length, 40);
  writeFileSync(file, Buffer.concat([wav, data]));
}

function rms(file) {
  const wav = readFileSync(file);
  assert.equal(wav.toString('ascii', 0, 4), 'RIFF');
  assert.equal(wav.toString('ascii', 8, 12), 'WAVE');
  const offset = wav.indexOf(Buffer.from('data')) + 8;
  assert.ok(offset >= 8);
  let sum = 0;
  let count = 0;
  for (let pos = offset + 44100 * 4; pos + 1 < wav.length; pos += 2) {
    const sample = wav.readInt16LE(pos) / 32768;
    sum += sample * sample;
    count++;
  }
  return Math.sqrt(sum / count);
}

test('le rendu WAV comprime un signal fort', { skip: !existsSync(sclang) }, async () => {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'timox-sc-'));
  const input = path.join(dir, 'input.wav');
  const output = path.join(dir, 'output.wav');
  try {
    writeWav(input, 0.6);
    assert.ok(existsSync(script), 'script de rendu WAV manquant');
    const result = await new Promise((resolve, reject) => {
      const child = spawn(sclang, ['-a', '--include-path', path.join(path.dirname(sclang), 'SCClassLibrary'), script, input, output], { windowsHide: true });
      let log = '';
      const timeout = setTimeout(() => { child.kill(); reject(new Error(`rendu trop long : ${log}`)); }, 60000);
      child.stdout.on('data', chunk => { log += chunk; });
      child.stderr.on('data', chunk => { log += chunk; });
      child.on('error', error => { clearTimeout(timeout); reject(error); });
      child.on('close', code => { clearTimeout(timeout); resolve({ code, log }); });
    });
    assert.equal(result.code, 0, result.log);
    assert.ok(existsSync(output), result.log);
    assert.ok(rms(output) < rms(input) * 0.8, `RMS entrée=${rms(input)} sortie=${rms(output)}`);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
