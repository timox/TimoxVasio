const { app, BrowserWindow, ipcMain, Menu } = require('electron');
const path = require('path');
const fs = require('fs');
const os = require('os');
const { launchEngine, releaseEngine } = require('./engine-process');
const { discoverEngine } = require('./engine-discovery');
const { validateRuntimeDependencies } = require('./runtime-dependencies');
const { inspectEngine, inspectProcess, terminateVerified, readReceipt, writeReceipt, prepareEngine, sameProcess } = require('./engine-ownership');
const WebSocket = require('ws');
const { ApiClient } = require('./api-client');

let mainWindow;
let engineProcess;
let apiPort;
let apiClient;
let engineStopping = false;
// Acquire before discovery: two launchers must not both observe a refused API
// connection and start competing audio engines.
// Keep the TVSD preview profile and its process lock across the product rename.
app.setPath('userData', path.join(app.getPath('appData'), 'timox-vsounddealer'));
app.setName('TimoxVSSC');
const ownsGuiLock = app.requestSingleInstanceLock();
if (!ownsGuiLock) app.quit();
app.on('second-instance', () => {
    if (!mainWindow || mainWindow.isDestroyed()) return;
    if (mainWindow.isMinimized()) mainWindow.restore();
    mainWindow.focus();
});
function createApiReadiness() {
    let resolve;
    let reject;
    const promise = new Promise((accept, fail) => { resolve = accept; reject = fail; });
    // Startup can fail before the renderer has attached its IPC handlers.
    promise.catch(() => {});
    return { promise, resolve, reject };
}
let apiReady = createApiReadiness();
const isDev = !app.isPackaged;
let runtimeDependencies;
const ownershipFile = path.join(app.getPath('userData'), 'engine-ownership.json');
const healthyEngine = () => discoverEngine(fetch, { ...runtimeDependencies, requiredSupervisionVersion: 1 });

function notify(channel, payload) {
    if (mainWindow && !mainWindow.isDestroyed()) mainWindow.webContents.send(channel, payload);
}

function startEngine() {
    if (engineProcess) return apiReady.promise;
    const readiness = apiReady;
    const enginePath = runtimeDependencies.enginePath;
    if (!fs.existsSync(enginePath)) {
        const error = new Error(`TimoxVirtualAsioEngine.exe is missing from the expected path: ${enginePath}`);
        console.error(error.message);
        readiness.reject(error);
        notify('engine-connection', { connected: false, error: error.message });
        return readiness.promise;
    }
    const child = engineProcess = launchEngine(enginePath, [], path.dirname(enginePath));
    let launchedIdentity;
    let confirming = false;
    let finished = false;
    let failed = false;
    const timeout = setTimeout(() => {
        finished = true; failed = true;
        const error = new Error('The verified engine did not become ready within 25 seconds.');
        readiness.reject(error);
        notify('engine-connection', { connected: false, error: error.message });
        if (launchedIdentity) terminateVerified(launchedIdentity).catch(failure => console.error(failure.message));
    }, 25000);
    child.on('spawn', async () => {
        try {
            const identity = await inspectProcess(child.pid);
            if (identity?.sha256 !== runtimeDependencies.engineSha256 || identity.path.toLowerCase() !== enginePath.toLowerCase())
                throw new Error('The launched engine does not match the verified dependency.');
            launchedIdentity = identity;
            if (failed) { await terminateVerified(identity); return; }
            if (!finished) writeReceipt(ownershipFile, identity);
        } catch (error) {
            if (finished && !failed) return;
            finished = true; failed = true; clearTimeout(timeout);
            readiness.reject(error);
            notify('engine-connection', { connected: false, error: error.message });
            if (launchedIdentity) terminateVerified(launchedIdentity).catch(failure => console.error(failure.message));
        }
    });
    let stdoutBuffer = '';
    child.stdout.on('data', data => {
        stdoutBuffer += data.toString();
        const lines = stdoutBuffer.split(/\r?\n/);
        stdoutBuffer = lines.pop();
        for (const line of lines) {
            try {
                const message = JSON.parse(line);
                if (message.event === 'api.ready' && message.port === 52525 && !confirming && !finished) {
                    confirming = true;
                    (async () => {
                        const observed = await inspectEngine();
                        if (observed.process?.pid !== child.pid || observed.process?.sha256 !== runtimeDependencies.engineSha256 ||
                            observed.process?.path?.toLowerCase() !== enginePath.toLowerCase())
                            throw new Error('The API listener does not belong to the verified engine.');
                        const port = await healthyEngine();
                        if (!port || !sameProcess(observed.process, (await inspectEngine()).process))
                            throw new Error('Engine identity changed during startup.');
                        if (finished) return;
                        finished = true; clearTimeout(timeout);
                        writeReceipt(ownershipFile, observed.process);
                        apiPort = port; readiness.resolve(port);
                        notify('engine-connection', { connected: true, port });
                    })().catch(error => {
                        finished = true; failed = true; clearTimeout(timeout); readiness.reject(error);
                        notify('engine-connection', { connected: false, error: error.message });
                        if (launchedIdentity) terminateVerified(launchedIdentity).catch(failure => console.error(failure.message));
                    });
                }
            } catch (_) {
                if (line.trim()) console.log(`VASIO: ${line}`);
            }
        }
    });
    child.stderr.on('data', data => {
        const message = data.toString();
        console.error(`VASIO: ${message}`);
        notify('engine-diagnostic', message);
    });
    child.on('error', error => {
        finished = true; failed = true; clearTimeout(timeout);
        readiness.reject(error);
        engineProcess = null;
        notify('engine-connection', { connected: false, error: error.message });
    });
    child.on('exit', (code, signal) => {
        finished = true; failed = true; clearTimeout(timeout);
        console.error(`VASIO engine exited: code=${code} signal=${signal}`);
        if (apiReady === readiness && !engineStopping)
            readiness.reject(new Error('TimoxVirtualAsioEngine stopped before the API started'));
        apiPort = undefined;
        engineProcess = null;
        if (apiClient) apiClient.close();
        apiClient = null;
        notify('engine-connection', { connected: false, code, signal });
    });
    return readiness.promise;
}

async function attachToExistingEngine() {
    const attached = await prepareEngine({
        engineSha256: runtimeDependencies.engineSha256,
        receipt: readReceipt(ownershipFile),
        healthy: healthyEngine,
        stopAudio: async identity => {
            const client = new ApiClient({ baseUrl: 'http://127.0.0.1:52525', webSocketUrl: 'ws://127.0.0.1:52525/api/v1/ws', WebSocket, timeoutMs: 3000 });
            try {
                await client.connect();
                if (!sameProcess(identity, (await inspectEngine()).process)) throw new Error('Engine identity changed before audio stop.');
                await client.stopEngine();
            } finally { client.close(); }
        },
        onReplacing: () => notify('engine-diagnostic', 'Updating the verified audio engine. Audio will pause briefly; saved routing is preserved.')
    });
    if (!attached) return false;
    writeReceipt(ownershipFile, attached.identity);
    apiPort = attached.port;
    apiReady.resolve(apiPort);
    return true;
}

async function getApiClient() {
    const port = await apiReady.promise;
    if (!apiClient) {
        apiClient = new ApiClient({
            baseUrl: `http://127.0.0.1:${port}`,
            webSocketUrl: `ws://127.0.0.1:${port}/api/v1/ws`,
            WebSocket
        });
        apiClient.on('event', event => notify('api:event', event));
        apiClient.on('disconnect', () => notify('api:disconnect'));
    }
    await apiClient.connect();
    return apiClient;
}

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1400, height: 900,
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            nodeIntegration: false,
            contextIsolation: true,
            enableRemoteModule: false
        }
    });
    mainWindow.on('page-title-updated', event => {
        event.preventDefault();
        mainWindow.setTitle(app.getName());
    });
    const startUrl = isDev
        ? (process.env.VASIO_CONTROL_URL || `http://localhost:${process.env.PORT || 4000}`)
        : `file://${path.join(__dirname, '../build/index.html')}`;
    mainWindow.loadURL(startUrl);
    if (isDev) mainWindow.webContents.openDevTools();
    mainWindow.on('closed', () => { mainWindow = null; });
}

ipcMain.handle('api:get-state', async () => (await getApiClient()).getState());
ipcMain.handle('api:get-drivers', async () => (await getApiClient()).getDrivers());
ipcMain.handle('api:get-wasapi-devices', async () => (await getApiClient()).getWasapiDevices());
ipcMain.handle('api:get-windows-audio-applications', async () => (await getApiClient()).getWindowsAudioApplications());
ipcMain.handle('system:get-network-addresses', () => Object.entries(os.networkInterfaces())
    .flatMap(([name, entries]) => (entries || []).map(entry => ({ name, ...entry })))
    .filter(entry => (entry.family === 'IPv4' || entry.family === 4) && !entry.internal && !entry.address.startsWith('169.254.'))
    .map(({ name, address }) => ({ name, address }))
    .filter((entry, index, addresses) => addresses.findIndex(candidate => candidate.name === entry.name && candidate.address === entry.address) === index));
ipcMain.handle('api:get-application-profiles', async () => (await getApiClient()).getApplicationProfiles());
ipcMain.handle('api:get-process-copy-templates', async () => (await getApiClient()).getProcessCopyTemplates());
ipcMain.handle('api:replace-process-copy-templates', async (_event, templates) =>
    (await getApiClient()).replaceProcessCopyTemplates(templates));
ipcMain.handle('api:replace-application-profiles', async (_event, profiles) =>
    (await getApiClient()).replaceApplicationProfiles(profiles));
ipcMain.handle('api:get-diagnostics', async (_event, limit) => (await getApiClient()).getDiagnostics(limit));
ipcMain.handle('api:set-diagnostic-level', async (_event, level) => (await getApiClient()).setDiagnosticLevel(level));
ipcMain.handle('api:execute-command', async (_event, command) => (await getApiClient()).sendCommand(command));
ipcMain.handle('api:http-request', async (_event, method, path, body) =>
    (await getApiClient()).httpRequest(method, path, body));
ipcMain.handle('api:configuration-apply', async (_event, configuration) =>
    (await getApiClient()).applyConfiguration(configuration));
ipcMain.handle('engine:start', async () => {
    const port = await apiReady.promise;
    await (await getApiClient()).sendCommand({ id: `engine-start-${Date.now()}`, command: 'engine.start' });
    return { running: true, port };
});
ipcMain.handle('engine:stop', async () => {
    const client = await getApiClient();
    await client.stopEngine();
    return { running: true, audioRunning: false, port: apiPort };
});
app.whenReady().then(async () => {
    if (!ownsGuiLock) return;
    createWindow();
    try {
        runtimeDependencies = validateRuntimeDependencies({ appRoot: path.resolve(__dirname, '..'), resourcesPath: process.resourcesPath, isPackaged: app.isPackaged });
        if (!await attachToExistingEngine()) await startEngine();
    } catch (error) {
        apiReady.reject(error);
        console.error(error.message);
        notify('engine-connection', { connected: false, error: error.message });
    }
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
app.on('activate', () => { if (ownsGuiLock && !mainWindow) createWindow(); });
app.on('before-quit', () => {
    if (apiClient) apiClient.close();
    if (engineProcess) releaseEngine(engineProcess);
});

Menu.setApplicationMenu(Menu.buildFromTemplate([
    { label: 'File', submenu: [{ label: 'Quit', accelerator: 'CmdOrCtrl+Q', click: () => app.quit() }] },
    { label: 'View', submenu: [
        { label: 'Reload', accelerator: 'CmdOrCtrl+R', click: () => mainWindow && mainWindow.reload() },
        { label: 'Developer Tools', accelerator: 'CmdOrCtrl+I', click: () => mainWindow && mainWindow.toggleDevTools() }
    ] }
]));
