const http = require('node:http');

const port = Number(process.argv[2]);
const processName = process.argv[3];
if (!Number.isInteger(port) || !processName) process.exit(2);

const state = {
  engine: {
    physicalDriverId: '{TEST-PHYSICAL-DRIVER}',
    sampleRate: 48000,
    bufferFrames: 1024,
  },
  virtualDrivers: [{ id: 'TimoxVasio', clients: [{ pid: 12345, processName }] }],
  configuredRoutes: [],
};

http.createServer((request, response) => {
  response.setHeader('content-type', 'application/json');
  if (request.method !== 'GET') {
    response.writeHead(405).end(JSON.stringify({ error: 'read-only fixture' }));
  } else if (request.url === '/api/v1/state') {
    response.writeHead(200).end(JSON.stringify(state));
  } else if (request.url === '/api/v1/application-profiles') {
    response.writeHead(200).end(JSON.stringify({ profiles: [] }));
  } else {
    response.writeHead(404).end(JSON.stringify({ error: 'unknown endpoint' }));
  }
}).listen(port, '127.0.0.1');
