#!/usr/bin/env node
// Compagnon local du Tool Renoise : API TimoxVasio (audio) + OSC vers sclang (MIDI).
import http from 'node:http';
import dgram from 'node:dgram';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HOST = '127.0.0.1';
const port = Number(process.env.TIMOX_RENOISE_PORT ?? 52999);
const api = process.env.TIMOX_API ?? 'http://127.0.0.1:52525';
const scPort = Number(process.env.SC_LANG_PORT ?? 57120);
const sclangPath = process.env.SCLANG_PATH ?? 'C:\\Program Files\\SuperCollider-3.14.1\\sclang.exe';
const scScript = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', 'supercollider', 'timox_chaine.scd');
const PREFIX = 'demo-renoise-sc-ableton-';
// Windows MIDI Services relie A à B. SC ouvre A en sortie ; les DAW ouvrent B en entrée.
const midiPairs = [
  { output: 'TimoxVasio SC vers Renoise', input: 'TimoxVasio Renoise Clock', id: 'timoxvasioscrenoise' },
  { output: 'TimoxVasio SC vers Ableton', input: 'TimoxVasio Ableton Clock', id: 'timoxvasioscableton' }
];
const execFileAsync = promisify(execFile);
let pending = null;
let scProcess = null;
let scLog = '';

function endpoint(client, direction, channel) {
  return `virtual:TimoxVasio:${client.pid}:${direction}:${channel}`;
}

function fingerprint(state) {
  return JSON.stringify({
    engine: state.engine,
    routes: state.configuredRoutes,
    virtual: state.virtualDrivers?.map(driver => ({ clients: driver.clients,
      inputs: driver.inputEndpoints?.map(item => item.id),
      outputs: driver.outputEndpoints?.map(item => item.id) })),
    physical: state.physicalDrivers?.map(driver => ({ id: driver.id,
      outputs: driver.outputEndpoints?.map(item => item.id) }))
  });
}

function clientByName(state, name) {
  const matches = (state.virtualDrivers ?? []).flatMap(driver => driver.clients ?? [])
    .filter(client => client.processName.toLowerCase() === name.toLowerCase());
  if (matches.length !== 1) throw new Error(`${name} : ${matches.length === 0 ? 'application ASIO absente' : 'plusieurs instances ouvertes'}`);
  return matches[0];
}

function requirePair(ids, first, label) {
  const second = first + 1;
  if (!Number.isInteger(first) || first < 1 || first > 255 ||
      !ids.has(first) || !ids.has(second)) {
    throw new Error(`${label} : paire ${first}-${second} non ouverte`);
  }
}

function channels(client, key) {
  return new Set(client[key] ?? []);
}

function route(id, sourceEndpointId, destinationEndpointId) {
  return { id: PREFIX + id, sourceEndpointId, destinationEndpointId, gainDb: 0, mute: false };
}

export function buildPlan(state, request) {
  if (!state?.engine?.physicalDriverId || !state.engine.sampleRate || !state.engine.bufferFrames) {
    throw new Error('Choisir et démarrer la SSL dans Timox VASIO Control avant de préparer la démo');
  }
  const renoise = clientByName(state, request.renoise);
  const sc = clientByName(state, request.sc);
  const ableton = clientByName(state, request.ableton);
  const physical = state.physicalDrivers.find(driver => driver.id === state.engine.physicalDriverId);
  if (!physical) throw new Error('Pilote physique sélectionné absent de l’inventaire');
  const physicalOutputs = new Set(physical.outputEndpoints.map(item => item.channel));
  const virtual = state.virtualDrivers.find(driver => driver.id === 'TimoxVasio');
  if (!virtual) throw new Error('Inventaire TimoxVasio absent');
  const inputIds = new Set(virtual.inputEndpoints.map(item => item.id));
  const outputIds = new Set(virtual.outputEndpoints.map(item => item.id));
  const originalRoutes = (state.configuredRoutes ?? []).filter(item => !item.id.startsWith(PREFIX));
  const physicalDestinations = new Set([request.physicalOutput, request.physicalOutput + 1]
    .map(channel => `physical:${physical.id}:output:${channel}`));
  const displaced = request.replacePhysicalRoutes
    ? originalRoutes.filter(item => physicalDestinations.has(item.destinationEndpointId)) : [];
  const oldRoutes = originalRoutes.filter(item => !displaced.includes(item));
  const usedDestinations = new Set(oldRoutes.map(item => item.destinationEndpointId));
  const warnings = [];
  if (displaced.length) warnings.push(`${displaced.length} route(s) existante(s) vers la paire SSL choisie seront retirées.`);
  const bandStarts = [request.low, request.mid, request.high];
  if (new Set(bandStarts).size !== 3) throw new Error('Choisir trois paires Renoise distinctes');
  for (let index = 0; index < 3; index++) {
    requirePair(channels(renoise, 'outputChannels'), bandStarts[index], `Renoise ${['grave', 'médium', 'aigu'][index]}`);
    requirePair(channels(sc, 'inputChannels'), index * 2 + 1, 'SC entrée');
    requirePair(channels(sc, 'outputChannels'), index * 2 + 1, 'SC sortie');
  }
  requirePair(channels(ableton, 'outputChannels'), request.abletonMaster, 'Ableton Master');
  requirePair(physicalOutputs, request.physicalOutput, 'SSL sortie');

  const freeAbletonPairs = [];
  const reusedAbletonPairs = [];
  for (let first = 1; first <= 255; first += 2) {
    if (!channels(ableton, 'inputChannels').has(first) || !channels(ableton, 'inputChannels').has(first + 1)) continue;
    const left = endpoint(ableton, 'input', first);
    const right = endpoint(ableton, 'input', first + 1);
    (usedDestinations.has(left) || usedDestinations.has(right) ? reusedAbletonPairs : freeAbletonPairs).push(first);
  }
  const abletonPairs = [...freeAbletonPairs, ...reusedAbletonPairs].slice(0, 3);
  if (abletonPairs.length !== 3) throw new Error('Ableton doit ouvrir au moins trois entrées stéréo TimoxVasio');
  if (abletonPairs.some(first => reusedAbletonPairs.includes(first))) warnings.push('Certaines entrées Ableton ont déjà une route enregistrée : le mélange reste autorisé.');

  const added = [];
  for (let band = 0; band < 3; band++) {
    const name = ['grave', 'medium', 'aigu'][band];
    for (let side = 0; side < 2; side++) {
      const renoiseOut = endpoint(renoise, 'output', bandStarts[band] + side);
      const scIn = endpoint(sc, 'input', band * 2 + 1 + side);
      const scOut = endpoint(sc, 'output', band * 2 + 1 + side);
      const abletonIn = endpoint(ableton, 'input', abletonPairs[band] + side);
      if (![renoiseOut, scOut].every(id => outputIds.has(id)) ||
          ![scIn, abletonIn].every(id => inputIds.has(id))) throw new Error('Un canal virtuel demandé est absent de l’inventaire actif');
      added.push(route(`${name}-renoise-sc-${side}`, renoiseOut, scIn));
      added.push(route(`${name}-sc-ableton-${side}`, scOut, abletonIn));
      if (usedDestinations.has(scIn)) warnings.push(`${name} : entrée SC ${band * 2 + 1 + side} déjà routée.`);
    }
  }
  for (let side = 0; side < 2; side++) {
    const source = endpoint(ableton, 'output', request.abletonMaster + side);
    const destination = `physical:${physical.id}:output:${request.physicalOutput + side}`;
    if (!outputIds.has(source) || !physical.outputEndpoints.some(item => item.id === destination)) {
      throw new Error('Sortie Master Ableton ou sortie SSL absente de l’inventaire actif');
    }
    added.push(route(`master-ssl-${side}`, source, destination));
    if (usedDestinations.has(destination)) warnings.push(`Sortie SSL ${request.physicalOutput + side} déjà routée : les signaux seront mélangés.`);
  }
  const payload = {
    physicalDriverId: state.engine.physicalDriverId,
    sampleRate: state.engine.sampleRate,
    bufferFrames: state.engine.bufferFrames,
    routes: [...oldRoutes, ...added]
  };
  return { payload, added, warnings: [...new Set(warnings)], abletonPairs, bandStarts,
    abletonMaster: request.abletonMaster, physicalOutput: request.physicalOutput,
    previousCount: oldRoutes.length, removedCount: displaced.length, snapshot: fingerprint(state) };
}

function numberParam(search, key) {
  const value = Number(search.get(key));
  if (!Number.isInteger(value) || value < 1 || value > 255 || value % 2 !== 1) {
    throw new Error(`${key} : numéro du premier canal stéréo attendu (1, 3, …, 255)`);
  }
  return value;
}

function requestFromUrl(search) {
  const name = key => {
    const value = (search.get(key) ?? '').trim();
    if (!/^[\w .()\-]{1,100}\.exe$/i.test(value)) throw new Error(`${key} : nom d’exécutable .exe attendu`);
    return value;
  };
  return { renoise: name('renoise'), sc: name('sc'), ableton: name('ableton'),
    low: numberParam(search, 'low'), mid: numberParam(search, 'mid'), high: numberParam(search, 'high'),
    abletonMaster: numberParam(search, 'abletonMaster'), physicalOutput: numberParam(search, 'physicalOutput'),
    replacePhysicalRoutes: search.get('replacePhysical') === '1' };
}

async function state() {
  const response = await fetch(new URL('/api/v1/state', api), { signal: AbortSignal.timeout(5000) });
  if (!response.ok) throw new Error(`API TimoxVasio : HTTP ${response.status}`);
  return response.json();
}

function command(commandName, payload) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(new URL('/api/v1/ws', api).href.replace(/^http/, 'ws'), 'vasio.api.v1');
    const id = `renoise-${Date.now()}-${Math.random()}`;
    const timer = setTimeout(() => { socket.close(); reject(new Error('API TimoxVasio : délai de réponse dépassé')); }, 12000);
    socket.onopen = () => socket.send(JSON.stringify({ id, command: commandName, payload }));
    socket.onmessage = event => {
      let message;
      try { message = JSON.parse(event.data); } catch { return; }
      if (message.id !== id) return;
      clearTimeout(timer);
      socket.close();
      message.success ? resolve(message.result) : reject(new Error(`${message.error?.code}: ${message.error?.message}`));
    };
    socket.onerror = () => { clearTimeout(timer); reject(new Error('Connexion WebSocket TimoxVasio impossible')); };
  });
}

function oscPad(buffer) { return Buffer.concat([buffer, Buffer.alloc((4 - buffer.length % 4) % 4)]); }
function osc(address, values) {
  const tags = ',' + values.map(value => typeof value === 'string' ? 's' : 'f').join('');
  const items = [oscPad(Buffer.from(address + '\0')), oscPad(Buffer.from(tags + '\0'))];
  for (const value of values) {
    if (typeof value === 'string') items.push(oscPad(Buffer.from(value + '\0')));
    else { const bytes = Buffer.alloc(4); bytes.writeFloatBE(value); items.push(bytes); }
  }
  return Buffer.concat(items);
}
function sendOsc(address, ...values) {
  return new Promise((resolve, reject) => {
    const socket = dgram.createSocket('udp4');
    socket.send(osc(address, values), scPort, HOST, error => {
      socket.close(); error ? reject(error) : resolve();
    });
  });
}

async function runMidi(args) {
  try {
    const { stdout } = await execFileAsync('midi', args, { timeout: 10000, windowsHide: true });
    return stdout;
  } catch (error) {
    throw new Error(`Windows MIDI Services : ${error.stderr?.trim() || error.message}`);
  }
}

export async function ensureMidiLoopbacks(run = runMidi, platform = process.platform) {
  if (platform !== 'win32') throw new Error('Préparation automatique MIDI disponible sur Windows seulement');
  let ports = await run(['enumerate', 'legacy-winrt-api-endpoints', '--direction', 'all']);
  const created = [];
  const present = name => ports.split(/\r?\n/).some(line => line.includes(` : ${name} `));
  for (const pair of midiPairs) {
    const hasOutput = present(pair.output);
    const hasInput = present(pair.input);
    if (hasOutput !== hasInput) throw new Error(`Paire MIDI incomplète : ${pair.output} / ${pair.input}`);
    if (!hasOutput) {
      await run(['loopback', 'create', '--name-a', pair.output, '--name-b', pair.input,
        '--unique-identifier', pair.id]);
      created.push(pair.input);
    }
  }
  if (created.length) ports = await run(['enumerate', 'legacy-winrt-api-endpoints', '--direction', 'all']);
  for (const pair of midiPairs) {
    if (!present(pair.output) || !present(pair.input)) {
      throw new Error(`Paire MIDI ${pair.output} / ${pair.input} absente après création`);
    }
  }
  return { outputs: midiPairs.map(pair => pair.output), inputs: midiPairs.map(pair => pair.input), created };
}

function oscTarget(search, prefix, fallbackPort) {
  const host = (search.get(`${prefix}Host`) ?? HOST).trim();
  const targetPort = Number(search.get(`${prefix}Port`) ?? fallbackPort);
  if (!['127.0.0.1', 'localhost'].includes(host) ||
      !Number.isInteger(targetPort) || targetPort < 1 || targetPort > 65535) {
    throw new Error(`${prefix} OSC : adresse locale et port TCP/UDP valides attendus`);
  }
  return { host, port: targetPort };
}

export function slip(packet) {
  const bytes = [0xc0];
  for (const byte of packet) {
    if (byte === 0xc0) bytes.push(0xdb, 0xdc);
    else if (byte === 0xdb) bytes.push(0xdb, 0xdd);
    else bytes.push(byte);
  }
  bytes.push(0xc0);
  return Buffer.from(bytes);
}

function sendOscTcp(target, address) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: target.host, port: target.port });
    socket.setTimeout(3000);
    socket.once('connect', () => socket.end(slip(osc(address, []))));
    socket.once('close', hadError => {
      if (!hadError) resolve();
    });
    socket.once('timeout', () => socket.destroy(new Error('délai TCP dépassé')));
    socket.once('error', reject);
  });
}

function sendOscUdp(target, address) {
  return new Promise((resolve, reject) => {
    const socket = dgram.createSocket('udp4');
    socket.send(osc(address, []), target.port, target.host, error => {
      socket.close();
      error ? reject(error) : resolve();
    });
  });
}

function scClientPresent(current) {
  return (current.virtualDrivers ?? []).some(driver =>
    (driver.clients ?? []).some(client => client.processName.toLowerCase() === 'scsynth.exe'));
}

async function startSc() {
  if (scProcess && scProcess.exitCode === null) return 'SuperCollider est déjà lancé par ce compagnon.';
  if (scClientPresent(await state())) return 'scsynth est déjà actif ; aucun second serveur lancé.';
  if (!existsSync(sclangPath)) throw new Error(`sclang absent : ${sclangPath} (définir SCLANG_PATH)`);
  scLog = '';
  const child = spawn(sclangPath, ['-D', '-u', String(scPort), scScript],
    { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  scProcess = child;
  const append = chunk => { scLog = (scLog + chunk.toString()).slice(-12000); };
  child.stdout.on('data', append);
  child.stderr.on('data', append);
  child.on('error', error => { append(error.message); });
  child.on('exit', () => { if (scProcess === child) scProcess = null; });
  for (let attempt = 0; attempt < 40; attempt++) {
    if (scLog.includes('Audio : 3 paires')) return 'SuperCollider démarré : 3 paires audio et horloge MIDI chargées.';
    if (child.exitCode !== null) throw new Error(`sclang s’est arrêté : ${scLog.slice(-1500)}`);
    if (scLog.includes('ERROR:') || scLog.includes('FAILURE IN SERVER')) {
      child.kill();
      throw new Error(`SuperCollider : ${scLog.slice(-1500)}`);
    }
    await new Promise(resolve => setTimeout(resolve, 500));
  }
  child.kill();
  throw new Error(`Démarrage SC non confirmé après 20 s : ${scLog.slice(-1500)}`);
}

function planText(plan) {
  const names = ['Grave', 'Médium', 'Aigu'];
  const lines = ['Aperçu : 14 routes audio ; les routes existantes sont conservées.'];
  for (let i = 0; i < 3; i++) {
    lines.push(`${names[i]} : Renoise Out ${plan.bandStarts[i]}-${plan.bandStarts[i] + 1} -> SC In ${i * 2 + 1}-${i * 2 + 2} -> SC Out ${i * 2 + 1}-${i * 2 + 2} -> Ableton In ${plan.abletonPairs[i]}-${plan.abletonPairs[i] + 1}`);
  }
  lines.push(`Ableton Master Out ${plan.abletonMaster}-${plan.abletonMaster + 1} -> SSL Out ${plan.physicalOutput}-${plan.physicalOutput + 1} ; ${plan.previousCount} autres routes conservées.`);
  lines.push(...plan.warnings.map(warning => `Attention : ${warning}`));
  lines.push('Cliquer Appliquer pour enregistrer cette configuration dans TimoxVasio.');
  return lines.join('\n');
}

export function createBridgeServer() {
  return http.createServer(async (request, response) => {
    response.setHeader('Content-Type', 'text/plain; charset=utf-8');
    response.setHeader('Cache-Control', 'no-store');
    response.setHeader('Connection', 'close');
    try {
      const url = new URL(request.url, `http://${HOST}:${port}`);
      if (request.method === 'GET' && url.pathname === '/state') {
        const current = await state();
        const clients = (current.virtualDrivers ?? []).flatMap(driver => driver.clients ?? []);
        response.end(`Moteur : ${current.engine.state} · ${current.engine.sampleRate ?? '?'} Hz · ${current.engine.bufferFrames ?? '?'} frames\n` +
          `Applications : ${clients.map(client => `${client.processName} (PID ${client.pid})`).join(', ') || 'aucune'}\n` +
          `Routes actives : ${(current.routes ?? []).length} ; enregistrées : ${(current.configuredRoutes ?? []).length}\n`);
      } else if (request.method === 'POST' && url.pathname === '/plan') {
        const current = await state();
        pending = buildPlan(current, requestFromUrl(url.searchParams));
        response.end(planText(pending) + '\n');
      } else if (request.method === 'POST' && url.pathname === '/apply') {
        if (!pending) throw new Error('Préparer un aperçu avant d’appliquer');
        const current = await state();
        if (fingerprint(current) !== pending.snapshot) { pending = null; throw new Error('État TimoxVasio modifié : refaire l’aperçu'); }
        const plan = pending;
        pending = null;
        await command('configuration.apply', plan.payload);
        const confirmed = await state();
        const ids = new Set((confirmed.configuredRoutes ?? []).map(item => item.id));
        const saved = plan.added.filter(item => ids.has(item.id)).length;
        if (saved !== plan.added.length) throw new Error(`API acceptée mais seulement ${saved}/14 routes confirmées`);
        response.end(`Configuration acceptée. ${saved}/14 routes de la démo enregistrées ; ${plan.removedCount} anciennes routes vers SSL retirées ; ${(confirmed.routes ?? []).length} routes actives. Voir Timox VASIO Control pour l’état appliqué.\n`);
      } else if (request.method === 'POST' && url.pathname === '/sc/start') {
        response.end((await startSc()) + '\n');
      } else if (request.method === 'POST' && url.pathname === '/sc/stop') {
        if (!scProcess || scProcess.exitCode !== null) throw new Error('Aucun SuperCollider lancé par ce compagnon');
        await sendOsc('/timox/demo/sc/stop');
        response.end('Arrêt demandé à SuperCollider.\n');
      } else if (request.method === 'POST' && url.pathname.startsWith('/osc/transport/')) {
        const action = url.pathname.slice('/osc/transport/'.length);
        if (!['start', 'stop'].includes(action)) throw new Error('Commande de transport OSC inconnue');
        const renoise = oscTarget(url.searchParams, 'renoise', 8000);
        const ableton = oscTarget(url.searchParams, 'ableton', 11000);
        await sendOscTcp(renoise, `/renoise/transport/${action}`);
        await sendOscUdp(ableton, action === 'start' ? '/live/song/start_playing' : '/live/song/stop_playing');
        response.end(`Transport OSC ${action} envoyé à Renoise (TCP) et AbletonOSC (UDP). L’horloge MIDI reste distincte.\n`);
      } else if (request.method === 'POST' && url.pathname.startsWith('/clock/')) {
        const action = url.pathname.slice('/clock/'.length);
        if (action === 'prepare') {
          if (!scProcess || scProcess.exitCode !== null) {
            throw new Error('Démarrer SC depuis ce Tool avant de préparer le MIDI');
          }
          const prepared = await ensureMidiLoopbacks();
          await sendOsc('/timox/demo/clock/auto-targets', ...prepared.outputs);
          response.end(`Paires MIDI ${prepared.created.length ? 'créées' : 'déjà présentes'}. Dans Renoise, choisir « ${prepared.inputs[0]} » en MIDI Clock Slave ; dans Live, choisir « ${prepared.inputs[1]} » en Sync In puis activer Ext.\n`);
        } else if (action === 'bpm') {
          const bpm = Number(url.searchParams.get('value'));
          if (!Number.isFinite(bpm) || bpm < 20 || bpm > 300) throw new Error('BPM attendu entre 20 et 300');
          await sendOsc('/timox/demo/clock/bpm', bpm);
        } else if (action === 'targets') {
          const targets = ['renoiseDevice', 'renoisePort', 'abletonDevice', 'abletonPort']
            .map(key => (url.searchParams.get(key) ?? '').trim());
          if (targets.some(value => !value || value.length > 150 || /[\x00-\x1f]/.test(value))) {
            throw new Error('Renseigner les noms MIDI device/port de Renoise et Ableton');
          }
          await sendOsc('/timox/demo/clock/targets', ...targets);
        } else if (action === 'start' || action === 'stop') {
          await sendOsc(`/timox/demo/clock/${action}`);
        } else throw new Error('Commande MIDI inconnue');
        response.end('Commande OSC envoyée à SuperCollider ; vérifier son journal pour confirmer la réception.\n');
      } else {
        response.writeHead(404).end('Commande inconnue\n');
      }
    } catch (error) {
      response.writeHead(400).end(`Erreur : ${error.message}\n`);
    }
  });
}

if (process.argv[1] && fileURLToPath(import.meta.url) === path.resolve(process.argv[1])) {
  createBridgeServer().listen(port, HOST, () => {
    console.log(`Tool Renoise : http://${HOST}:${port} -> API ${api} ; SC OSC ${scPort}`);
  });
}
