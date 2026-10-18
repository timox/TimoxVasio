#!/usr/bin/env node
// Pilotage de TimoxVasio (API locale) et du compresseur SuperCollider, sans dépendance.
// Prérequis : Node.js 18+ (fetch) ; Node 22+ recommandé (WebSocket natif).
//
// Variables d'environnement :
//   TIMOX_API   URL de base de l'API, par ex. http://127.0.0.1:52341 (port annoncé par le moteur)
//   SC_HOTE     hôte scsynth/sclang (défaut 127.0.0.1)
//   SC_PORT     port OSC du serveur SuperCollider (défaut 57110)
//
// Commandes :
//   node pilote.mjs etat                   état du moteur, clients et routes (GET /api/v1/state)
//   node pilote.mjs demarrer               engine.start (WebSocket)
//   node pilote.mjs arreter                engine.stop (WebSocket)
//   node pilote.mjs sc-demarrer            crée le compresseur (nœud 1000) dans SuperCollider
//   node pilote.mjs sc-regler <clé> <val>  règle un paramètre, ex. loThr -30 ou outDb 3
//   node pilote.mjs sc-bypass <0|1>        1 = bypass
//   node pilote.mjs sc-arreter             libère le nœud 1000

import dgram from 'node:dgram';

const NOEUD_SC = 1000;
const API = process.env.TIMOX_API ?? '';
const SC_HOTE = process.env.SC_HOTE ?? '127.0.0.1';
const SC_PORT = Number(process.env.SC_PORT ?? 57110);

// ---------- API TimoxVasio ----------

function exigerApi() {
  if (!API) {
    console.error('Définir TIMOX_API, par ex. TIMOX_API=http://127.0.0.1:52341');
    process.exit(1);
  }
  return API.replace(/\/$/, '');
}

async function etat() {
  const reponse = await fetch(`${exigerApi()}/api/v1/state`);
  if (!reponse.ok) throw new Error(`GET /api/v1/state : HTTP ${reponse.status}`);
  const e = await reponse.json();
  console.log(JSON.stringify(e, null, 2));
}

// Envoie une commande WebSocket et attend l'acquittement portant le même id.
function commandeApi(command, payload) {
  const url = exigerApi().replace(/^http/, 'ws') + '/api/v1/ws';
  const id = `node-${Date.now()}`;
  return new Promise((resoudre, rejeter) => {
    const ws = new WebSocket(url);
    const delai = setTimeout(() => { ws.close(); rejeter(new Error('délai dépassé')); }, 10000);
    ws.onopen = () => ws.send(JSON.stringify({ id, command, ...(payload ? { payload } : {}) }));
    ws.onmessage = (msg) => {
      const r = JSON.parse(msg.data);
      if (r.id !== id) return; // les événements (engine.status, etc.) sont ignorés
      clearTimeout(delai);
      ws.close();
      r.success ? resoudre(r.result) : rejeter(new Error(`${r.error.code} : ${r.error.message}`));
    };
    ws.onerror = () => { clearTimeout(delai); rejeter(new Error(`connexion impossible à ${url}`)); };
  });
}

// ---------- OSC minimal vers scsynth ----------

function padOsc(buf) {
  return Buffer.concat([buf, Buffer.alloc((4 - (buf.length % 4)) % 4)]);
}

const PARAMETRES = {
  inDb: [-24, 24], outDb: [-24, 24],
  xo1: [60, 800], xo2: [800, 8000],
  loThr: [-60, 0], midThr: [-60, 0], hiThr: [-60, 0],
  loRatio: [1, 20], midRatio: [1, 20], hiRatio: [1, 20],
};

function nombreFini(texte) {
  if (texte === undefined || texte.trim() === '') throw new Error('valeur numérique requise');
  const valeur = Number(texte);
  if (!Number.isFinite(valeur)) throw new Error('valeur numérique finie requise');
  return valeur;
}

// Encode un message OSC : adresse, tags de type, arguments (string, entier ou flottant).
function encoderOsc(adresse, args) {
  const morceaux = [padOsc(Buffer.from(adresse + '\0'))];
  let tags = ',';
  const valeurs = [];
  for (const a of args) {
    if (typeof a === 'string') {
      tags += 's';
      valeurs.push(padOsc(Buffer.from(a + '\0')));
    } else if (Number.isInteger(a)) {
      tags += 'i';
      const b = Buffer.alloc(4); b.writeInt32BE(a); valeurs.push(b);
    } else {
      tags += 'f';
      const b = Buffer.alloc(4); b.writeFloatBE(a); valeurs.push(b);
    }
  }
  morceaux.push(padOsc(Buffer.from(tags + '\0')), ...valeurs);
  return Buffer.concat(morceaux);
}

function envoyerOsc(adresse, args) {
  return new Promise((resoudre, rejeter) => {
    const socket = dgram.createSocket('udp4');
    let fini = false;
    const terminer = (erreur) => {
      if (fini) return;
      fini = true;
      clearTimeout(delai);
      socket.close();
      erreur ? rejeter(erreur) : resoudre();
    };
    const delai = setTimeout(() => terminer(new Error(`scsynth ne répond pas sur ${SC_HOTE}:${SC_PORT}`)), 1500);
    socket.on('error', terminer);
    socket.on('message', (reponse) => {
      if (reponse.toString('ascii', 0, 13) !== '/status.reply') return;
      socket.send(encoderOsc(adresse, args), SC_PORT, SC_HOTE, terminer);
    });
    socket.send(encoderOsc('/status', []), SC_PORT, SC_HOTE, err => { if (err) terminer(err); });
  });
}

// ---------- commandes ----------

const [commande, ...args] = process.argv.slice(2);

const actions = {
  'etat': () => etat(),
  'demarrer': async () => console.log(await commandeApi('engine.start')),
  'arreter': async () => console.log(await commandeApi('engine.stop')),
  'sc-demarrer': () => envoyerOsc('/s_new', ['timoxCompresseur', NOEUD_SC, 0, 0]),
  'sc-regler': () => {
    const [cle, valeur] = args;
    if (!cle || valeur === undefined) throw new Error('usage : sc-regler <clé> <valeur>');
    if (!Object.hasOwn(PARAMETRES, cle)) throw new Error(`paramètre inconnu : ${cle}`);
    const plage = PARAMETRES[cle];
    const nombre = nombreFini(valeur);
    if (nombre < plage[0] || nombre > plage[1]) throw new Error(`${cle} hors plage : ${plage[0]} à ${plage[1]}`);
    return envoyerOsc('/n_set', [NOEUD_SC, cle, nombre]);
  },
  'sc-bypass': () => {
    if (!['0', '1'].includes(args[0])) throw new Error('usage : sc-bypass <0|1>');
    return envoyerOsc('/n_set', [NOEUD_SC, 'bypass', Number(args[0])]);
  },
  'sc-arreter': () => envoyerOsc('/n_free', [NOEUD_SC]),
};

if (!actions[commande]) {
  console.log('Commandes : ' + Object.keys(actions).join(', '));
  process.exit(commande ? 1 : 0);
}

try {
  await actions[commande]();
  if (commande.startsWith('sc-')) console.log(`Commande envoyée : ${commande} ${args.join(' ')}`);
} catch (err) {
  console.error('Erreur :', err.message);
  process.exit(1);
}
