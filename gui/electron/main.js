const { app, BrowserWindow, ipcMain, Menu } = require('electron');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const WebSocket = require('ws');
const { ApiClient } = require('./api-client');

let mainWindow;
let engineProcess;
let apiPort;
let apiClient;
let engineStopping = false;
function createApiReadiness() {
    let resolve;
    let reject;
    const promise = new Promise((accept, fail) => { resolve = accept; reject = fail; });
    return { promise, resolve, reject };
}
let apiReady = createApiReadiness();
const isDev = !app.isPackaged;

function notify(channel, payload) {
    if (mainWindow && !mainWindow.isDestroyed()) mainWindow.webContents.send(channel, payload);
}

function startEngine() {
    if (engineProcess) return apiReady.promise;
    apiReady = createApiReadiness();
    const readiness = apiReady;
    const enginePath = isDev
        ? path.resolve(__dirname, '../../build_codex_110/Release/TimoxVirtualAsioEngine.exe')
        : path.join(process.resourcesPath, 'backend/TimoxVirtualAsioEngine.exe');
    if (!fs.existsSync(enginePath)) {
        const error = new Error(`TimoxVirtualAsioEngine.exe is missing from the expected path: ${enginePath}`);
        console.error(error.message);
        readiness.reject(error);
        notify('engine-connection', { connected: false, error: error.message });
        return readiness.promise;
    }
    engineProcess = spawn(enginePath, [], { stdio: ['ignore', 'pipe', 'pipe'], detached: false, cwd: path.dirname(enginePath) });
    engineProcess.on('spawn', () => console.log(`VASIO engine started: ${enginePath} (PID ${engineProcess.pid})`));
    let stdoutBuffer = '';
    engineProcess.stdout.on('data', data => {
        stdoutBuffer += data.toString();
        const lines = stdoutBuffer.split(/\r?\n/);
        stdoutBuffer = lines.pop();
        for (const line of lines) {
            try {
                const message = JSON.parse(line);
                if (message.event === 'api.ready' && Number.isInteger(message.port)) {
                    apiPort = message.port;
                    readiness.resolve(apiPort);
                    notify('engine-connection', { connected: true, port: apiPort });
                }
            } catch (_) {
                if (line.trim()) console.log(`VASIO: ${line}`);
            }
        }
    });
    engineProcess.stderr.on('data', data => {
        const message = data.toString();
        console.error(`VASIO: ${message}`);
        notify('engine-diagnostic', message);
    });
    engineProcess.on('error', error => {
        readiness.reject(error);
        engineProcess = null;
        notify('engine-connection', { connected: false, error: error.message });
    });
    engineProcess.on('exit', (code, signal) => {
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
    const port = 52525;
    try {
        const response = await fetch(`http://127.0.0.1:${port}/api/v1/state`, {
            signal: AbortSignal.timeout(800)
        });
        if (!response.ok) return false;
        const state = await response.json();
        const isTimoxVasioEngine = state.apiVersion === '1.0' &&
            Array.isArray(state.virtualDrivers) &&
            state.virtualDrivers.some(driver => driver.id === 'TimoxVasio');
        if (!isTimoxVasioEngine) return false;
        apiPort = port;
        apiReady.resolve(port);
        console.log(`Attached to existing TimoxVasio engine on 127.0.0.1:${port}`);
        return true;
    } catch (_) {
        return false;
    }
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
    const startUrl = isDev
        ? (process.env.VASIO_CONTROL_URL || `http://localhost:${process.env.PORT || 4000}`)
        : `file://${path.join(__dirname, '../build/index.html')}`;
    mainWindow.loadURL(startUrl);
    if (isDev) mainWindow.webContents.openDevTools();
    mainWindow.on('closed', () => { mainWindow = null; });
}

ipcMain.handle('api:get-state', async () => (await getApiClient()).getState());
ipcMain.handle('api:get-drivers', async () => (await getApiClient()).getDrivers());
ipcMain.handle('api:get-application-profiles', async () => (await getApiClient()).getApplicationProfiles());
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
    const port = apiPort !== undefined ? apiPort : await startEngine();
    await (await getApiClient()).sendCommand({ id: `engine-start-${Date.now()}`, command: 'engine.start' });
    return { running: true, port };
});
ipcMain.handle('engine:stop', async () => {
    const client = await getApiClient();
    await client.stopEngine();
    return { running: true, audioRunning: false, port: apiPort };
});
app.whenReady().then(async () => {
    if (!await attachToExistingEngine()) startEngine();
    createWindow();
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
app.on('activate', () => { if (!mainWindow) createWindow(); });
app.on('before-quit', () => {
    if (apiClient) apiClient.close();
    if (engineProcess) {
        engineProcess.unref();
        if (engineProcess.stdout?._handle) engineProcess.stdout._handle.unref();
        if (engineProcess.stderr?._handle) engineProcess.stderr._handle.unref();
    }
});

Menu.setApplicationMenu(Menu.buildFromTemplate([
    { label: 'File', submenu: [{ label: 'Quit', accelerator: 'CmdOrCtrl+Q', click: () => app.quit() }] },
    { label: 'View', submenu: [
        { label: 'Reload', accelerator: 'CmdOrCtrl+R', click: () => mainWindow && mainWindow.reload() },
        { label: 'Developer Tools', accelerator: 'CmdOrCtrl+I', click: () => mainWindow && mainWindow.toggleDevTools() }
    ] }
]));
