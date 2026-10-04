const { app, BrowserWindow, ipcMain, Menu } = require('electron');
const path = require('path');
const { spawn } = require('child_process');
const WebSocket = require('ws');
const { ApiClient } = require('./api-client');

let mainWindow;
let engineProcess;
let apiPort;
let apiClient;
let apiReadyResolve;
let apiReadyReject;
const isDev = !app.isPackaged;
const apiReady = new Promise((resolve, reject) => {
    apiReadyResolve = resolve;
    apiReadyReject = reject;
});

function notify(channel, payload) {
    if (mainWindow && !mainWindow.isDestroyed()) mainWindow.webContents.send(channel, payload);
}

function startEngine() {
    const enginePath = isDev
        ? path.join(__dirname, '../../build_engine_vs2026_ninja/TimoxVirtualAsioEngine.exe')
        : path.join(process.resourcesPath, 'backend/TimoxVirtualAsioEngine.exe');
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
                    apiReadyResolve(apiPort);
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
        apiReadyReject(error);
        notify('engine-connection', { connected: false, error: error.message });
    });
    engineProcess.on('exit', (code, signal) => {
        console.error(`VASIO engine exited: code=${code} signal=${signal}`);
        apiReadyReject(new Error('TimoxVirtualAsioEngine s’est arrêté avant le démarrage de l’API'));
        apiPort = undefined;
        engineProcess = null;
        notify('engine-connection', { connected: false, code, signal });
    });
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
        apiReadyResolve(port);
        console.log(`Attached to existing TimoxVasio engine on 127.0.0.1:${port}`);
        return true;
    } catch (_) {
        return false;
    }
}

async function getApiClient() {
    const port = await apiReady;
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
ipcMain.handle('api:configuration-apply', async (_event, configuration) =>
    (await getApiClient()).applyConfiguration(configuration));
app.whenReady().then(async () => {
    if (!await attachToExistingEngine()) startEngine();
    createWindow();
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
app.on('activate', () => { if (!mainWindow) createWindow(); });
app.on('before-quit', () => {
    if (apiClient) apiClient.close();
    if (engineProcess) engineProcess.kill();
});

Menu.setApplicationMenu(Menu.buildFromTemplate([
    { label: 'Fichier', submenu: [{ label: 'Quitter', accelerator: 'CmdOrCtrl+Q', click: () => app.quit() }] },
    { label: 'Affichage', submenu: [
        { label: 'Recharger', accelerator: 'CmdOrCtrl+R', click: () => mainWindow && mainWindow.reload() },
        { label: 'Outils de développement', accelerator: 'CmdOrCtrl+I', click: () => mainWindow && mainWindow.toggleDevTools() }
    ] }
]));
