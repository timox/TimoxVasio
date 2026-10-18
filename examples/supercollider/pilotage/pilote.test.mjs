import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import dgram from 'node:dgram';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const script = fileURLToPath(new URL('./pilote.mjs', import.meta.url));

function run(args, env = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [script, ...args], {
      env: { ...process.env, ...env },
      windowsHide: true,
    });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', chunk => { stdout += chunk; });
    child.stderr.on('data', chunk => { stderr += chunk; });
    child.on('error', reject);
    child.on('close', code => resolve({ code, stdout, stderr }));
  });
}

test('la commande OSC aligne les chaînes sans ajouter de bloc vide', async () => {
  const socket = dgram.createSocket('udp4');
  await new Promise(resolve => socket.bind(0, '127.0.0.1', resolve));
  try {
    const packet = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('aucun paquet OSC')), 2000);
      socket.on('message', (data, peer) => {
        if (data.toString('ascii', 0, 7) === '/status') {
          socket.send(Buffer.from('/status.reply\0\0\0,\0\0\0'), peer.port, peer.address);
          return;
        }
        clearTimeout(timeout);
        resolve(data);
      });
    });
    const result = await run(['sc-arreter'], { SC_PORT: String(socket.address().port) });
    assert.equal(result.code, 0, result.stderr);
    const data = await packet;
    assert.equal(data.subarray(0, 8).toString('ascii'), '/n_free\0');
    assert.equal(data.subarray(8, 12).toString('ascii'), ',i\0\0');
    assert.equal(data.readInt32BE(12), 1000);
  } finally {
    socket.close();
  }
});

test('sc-regler refuse les valeurs non numériques', async () => {
  const result = await run(['sc-regler', 'loThr', 'invalide']);
  assert.notEqual(result.code, 0);
  assert.match(result.stderr, /valeur|nombre/i);
});

test('sc-regler refuse un ratio hors de la plage de l’interface', async () => {
  const result = await run(['sc-regler', 'loRatio', '0']);
  assert.notEqual(result.code, 0);
  assert.match(result.stderr, /plage|ratio/i);
});

test('sc-regler refuse une clé qui n’est pas un paramètre', async () => {
  const result = await run(['sc-regler', 'toString', '1']);
  assert.notEqual(result.code, 0);
  assert.match(result.stderr, /paramètre inconnu/i);
});

test('une commande OSC échoue si scsynth ne répond pas', async () => {
  const socket = dgram.createSocket('udp4');
  await new Promise(resolve => socket.bind(0, '127.0.0.1', resolve));
  const port = socket.address().port;
  socket.close();
  const result = await run(['sc-bypass', '1'], { SC_PORT: String(port) });
  assert.notEqual(result.code, 0);
  assert.match(result.stderr, /scsynth|répond|connexion/i);
});
