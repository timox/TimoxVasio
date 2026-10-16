const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { JSDOM } = require('../../gui/node_modules/jsdom');

const pagePath = path.join(__dirname, 'index.html');
const state = {
  apiVersion: '1.0',
  engine: { state: 'stopped', physicalDriverId: 'ssl', sampleRate: 48000, bufferFrames: 256, lastError: null },
  physicalDrivers: [{ id: 'ssl', name: 'SSL ASIO', inputEndpoints: [], outputEndpoints: [], sampleRates: [48000], bufferSizes: null, capabilitiesKnown: true }],
  virtualDrivers: [{ id: 'TimoxVasio', channelsPerDirection: 256, sampleRate: 48000, bufferFrames: 256, clients: [{ pid: 42, processName: 'renoise.exe', inputChannels: [], outputChannels: [1, 2] }], inputEndpoints: [], outputEndpoints: [] }],
  routes: [{ id: 'r1', sourceEndpointId: 'virtual:TimoxVasio:42:output:1', destinationEndpointId: 'physical:ssl:output:1', gainDb: -3, mute: false }],
  configuredRoutes: [], stereoPairs: [], bufferHealth: null,
  performanceMeasurements: { enabled: false, sampleEveryCallbacks: 16, window: null }
};

async function until(check) {
  for (let i = 0; i < 50; i += 1) {
    if (check()) return;
    await new Promise(resolve => setTimeout(resolve, 10));
  }
  assert.fail('La page ne s’est pas mise à jour');
}

function openPage() {
  const requests = [];
  const sockets = [];
  class LocalSocket {
    static OPEN = 1;
    constructor(url, protocol) {
      this.url = url;
      this.protocol = protocol;
      this.readyState = 0;
      this.sent = [];
      sockets.push(this);
      queueMicrotask(() => {
        this.readyState = 1;
        this.onopen?.();
      });
    }
    send(data) { this.sent.push(JSON.parse(data)); }
    close() {
      this.readyState = 3;
      this.onclose?.();
    }
    receive(message) { this.onmessage?.({ data: JSON.stringify(message) }); }
  }
  const html = fs.existsSync(pagePath) ? fs.readFileSync(pagePath, 'utf8') : '';
  const dom = new JSDOM(html, {
    url: 'file:///C:/TimoxVasio/examples/api-web/index.html',
    runScripts: 'dangerously',
    beforeParse(window) {
      window.WebSocket = LocalSocket;
      window.fetch = async url => {
        requests.push(url);
        return { ok: true, json: async () => structuredClone(state) };
      };
    }
  });
  return { dom, requests, sockets };
}

test('le fichier local se connecte à l’API, montre son état et envoie engine.start', async () => {
  const { dom, requests, sockets } = openPage();
  const { document } = dom.window;
  const connect = document.querySelector('#connect');
  assert.ok(connect, 'La page doit offrir un bouton de connexion');
  connect.click();
  await until(() => document.querySelector('#clients')?.textContent.includes('renoise.exe'));

  assert.deepEqual(requests, ['http://127.0.0.1:52525/api/v1/state']);
  assert.equal(sockets[0].url, 'ws://127.0.0.1:52525/api/v1/ws');
  assert.equal(sockets[0].protocol, 'vasio.api.v1');
  assert.match(document.querySelector('#routes').textContent, /-3 dB/);
  assert.match(document.querySelector('#engine-state').textContent, /Arrêté/);

  document.querySelector('#start').click();
  assert.equal(sockets[0].sent.length, 1);
  assert.equal(sockets[0].sent[0].command, 'engine.start');
  assert.ok(sockets[0].sent[0].id);
  sockets[0].receive({ id: sockets[0].sent[0].id, success: true, result: { accepted: true } });
  sockets[0].receive({ event: 'engine.status', payload: { ...state.engine, state: 'running' } });
  await until(() => document.querySelector('#engine-state').textContent.includes('En marche'));
  assert.match(document.querySelector('#exchange-log').textContent, /engine.start/);
  dom.window.close();
});

test('un port incorrect est refusé avant tout appel réseau', async () => {
  const { dom, requests, sockets } = openPage();
  const { document } = dom.window;
  assert.ok(document.querySelector('#port'), 'La page doit demander le port local');
  document.querySelector('#port').value = '70000';
  document.querySelector('#connect').click();
  await until(() => document.querySelector('#connection-status')?.textContent.includes('port'));
  assert.equal(requests.length, 0);
  assert.equal(sockets.length, 0);
  dom.window.close();
});

test('une perte de connexion désactive les commandes', async () => {
  const { dom, sockets } = openPage();
  const { document } = dom.window;
  assert.ok(document.querySelector('#connect'));
  document.querySelector('#connect').click();
  await until(() => !document.querySelector('#start')?.disabled);
  sockets[0].close();
  await until(() => document.querySelector('#start').disabled);
  assert.match(document.querySelector('#connection-status').textContent, /déconnecté/i);
  dom.window.close();
});
