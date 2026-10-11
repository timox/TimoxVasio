const http = require('node:http');

const port = Number(process.argv[2]);
const ghostPid = Number(process.argv[3]);
if (!Number.isInteger(port) || !Number.isInteger(ghostPid)) process.exit(2);

const state = {
  virtualDrivers: [{
    id: 'TimoxVasio',
    clients: [{ pid: ghostPid, processName: 'vanished-test-client.exe' }],
  }],
  configuredRoutes: [],
};
http.createServer((request, response) => {
  if (request.url !== '/api/v1/state') {
    response.writeHead(404).end();
    return;
  }
  response.writeHead(200, { 'content-type': 'application/json' });
  response.end(JSON.stringify(state));
}).listen(port, '127.0.0.1');
