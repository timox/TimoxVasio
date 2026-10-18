import assert from 'node:assert/strict';
import net from 'node:net';
import dgram from 'node:dgram';
import test from 'node:test';
import { buildPlan, createBridgeServer, ensureMidiLoopbacks, slip } from './bridge.mjs';

test('prépare deux ports MIDI dédiés et évite les doublons', async () => {
  const names = new Set(['TimoxVasio SC vers Renoise', 'TimoxVasio Renoise Clock']);
  const calls = [];
  const run = async args => {
    calls.push(args);
    if (args[0] === 'loopback') { names.add(args[3]); names.add(args[5]); }
    return [...names].map((name, index) => `│ Port ${index} : ${name} Message Destination │`).join('\n');
  };
  const first = await ensureMidiLoopbacks(run, 'win32');
  assert.deepEqual(first.created, ['TimoxVasio Ableton Clock']);
  assert.deepEqual(first.outputs, ['TimoxVasio SC vers Renoise', 'TimoxVasio SC vers Ableton']);
  assert.deepEqual(first.inputs, ['TimoxVasio Renoise Clock', 'TimoxVasio Ableton Clock']);
  const second = await ensureMidiLoopbacks(run, 'win32');
  assert.deepEqual(second.created, []);
  assert.equal(calls.filter(args => args[0] === 'loopback').length, 1);
  await assert.rejects(ensureMidiLoopbacks(async () =>
    '│ Port 0 : TimoxVasio SC vers Renoise Message Destination │', 'win32'), /incomplète/);
});

function fixture() {
  const clients = [
    { pid: 11, processName: 'Renoise.exe', inputChannels: [], outputChannels: [1, 2, 3, 4, 5, 6] },
    { pid: 22, processName: 'scsynth.exe', inputChannels: [1, 2, 3, 4, 5, 6], outputChannels: [1, 2, 3, 4, 5, 6] },
    { pid: 33, processName: 'Ableton Live 12 Suite.exe', inputChannels: [1, 2, 3, 4, 5, 6, 7, 8], outputChannels: [1, 2] }
  ];
  const endpoint = (client, direction, channel) => ({
    id: `virtual:TimoxVasio:${client.pid}:${direction}:${channel}`, channel
  });
  return {
    engine: { state: 'running', physicalDriverId: 'ssl', sampleRate: 48000, bufferFrames: 256 },
    configuredRoutes: [{ id: 'existing', sourceEndpointId: 'virtual:TimoxVasio:11:output:1',
      destinationEndpointId: 'virtual:TimoxVasio:33:input:1', gainDb: 0, mute: false }],
    routes: [],
    physicalDrivers: [{ id: 'ssl', outputEndpoints: [1, 2].map(channel => ({ id: `physical:ssl:output:${channel}`, channel })) }],
    virtualDrivers: [{ id: 'TimoxVasio', clients,
      inputEndpoints: clients.flatMap(client => client.inputChannels.map(channel => endpoint(client, 'input', channel))),
      outputEndpoints: clients.flatMap(client => client.outputChannels.map(channel => endpoint(client, 'output', channel))) }]
  };
}

const request = { renoise: 'Renoise.exe', sc: 'scsynth.exe', ableton: 'Ableton Live 12 Suite.exe',
  low: 1, mid: 3, high: 5, abletonMaster: 1, physicalOutput: 1 };

test('préserve les routes existantes et réserve trois entrées Ableton libres', () => {
  const plan = buildPlan(fixture(), request);
  assert.equal(plan.added.length, 14);
  assert.equal(plan.payload.routes.length, 15);
  assert.equal(plan.payload.routes[0].id, 'existing');
  assert.deepEqual(plan.abletonPairs, [3, 5, 7]);
  assert.equal(plan.payload.physicalDriverId, 'ssl');
  assert.equal(plan.added.at(-1).destinationEndpointId, 'physical:ssl:output:2');
});

test('refuse une source stéréo absente ou la réutilisation de la même paire', () => {
  assert.throws(() => buildPlan(fixture(), { ...request, mid: 1 }), /distinctes/);
  assert.throws(() => buildPlan(fixture(), { ...request, high: 7 }), /non ouverte/);
});

test('remplace les anciennes routes de la démo sans toucher aux autres', () => {
  const state = fixture();
  state.configuredRoutes.push({ id: 'demo-renoise-sc-ableton-ancien', sourceEndpointId: 'x', destinationEndpointId: 'y', gainDb: 0, mute: false });
  const plan = buildPlan(state, request);
  assert.equal(plan.payload.routes.length, 15);
  assert.ok(!plan.payload.routes.some(route => route.id.endsWith('ancien')));
});

test('retire les anciennes routes vers la paire SSL seulement si demandé', () => {
  const state = fixture();
  state.configuredRoutes.push({ id: 'sc-vers-ssl', sourceEndpointId: 'sc-out-1',
    destinationEndpointId: 'physical:ssl:output:1', gainDb: 0, mute: false });
  assert.equal(buildPlan(state, request).removedCount, 0);
  const plan = buildPlan(state, { ...request, replacePhysicalRoutes: true });
  assert.equal(plan.removedCount, 1);
  assert.ok(!plan.payload.routes.some(route => route.id === 'sc-vers-ssl'));
  assert.ok(plan.payload.routes.some(route => route.id === 'existing'));
});

test('encadre OSC sur TCP avec SLIP', () => {
  assert.deepEqual([...slip(Buffer.from([0xc0, 0xdb, 0x01]))],
    [0xc0, 0xdb, 0xdc, 0xdb, 0xdd, 0x01, 0xc0]);
});

test('envoie Start OSC à Renoise TCP et AbletonOSC UDP aux ports choisis', async () => {
  let tcpPacket;
  let udpPacket;
  const tcp = net.createServer(socket => socket.on('data', chunk => { tcpPacket = chunk; }));
  const udp = dgram.createSocket('udp4');
  const bridge = createBridgeServer();
  await Promise.all([
    new Promise(resolve => tcp.listen(0, '127.0.0.1', resolve)),
    new Promise(resolve => udp.bind(0, '127.0.0.1', resolve)),
    new Promise(resolve => bridge.listen(0, '127.0.0.1', resolve))
  ]);
  udp.on('message', message => { udpPacket = message; });
  try {
    const query = new URLSearchParams({ renoiseHost: '127.0.0.1',
      renoisePort: String(tcp.address().port), abletonHost: '127.0.0.1',
      abletonPort: String(udp.address().port) });
    const response = await fetch(`http://127.0.0.1:${bridge.address().port}/osc/transport/start?${query}`, { method: 'POST' });
    assert.equal(response.status, 200);
    await new Promise(resolve => setTimeout(resolve, 30));
    assert.equal(tcpPacket[0], 0xc0);
    assert.equal(tcpPacket.at(-1), 0xc0);
    assert.ok(tcpPacket.includes(Buffer.from('/renoise/transport/start')));
    assert.ok(udpPacket.includes(Buffer.from('/live/song/start_playing')));
  } finally {
    bridge.close();
    tcp.close();
    udp.close();
  }
});
