const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('electronAPI', {
    onEngineConnection: callback => {
        const listener = (_event, payload) => callback(payload);
        ipcRenderer.on('engine-connection', listener);
        return () => ipcRenderer.removeListener('engine-connection', listener);
    },
    onEngineDiagnostic: callback => {
        const listener = (_event, payload) => callback(payload);
        ipcRenderer.on('engine-diagnostic', listener);
        return () => ipcRenderer.removeListener('engine-diagnostic', listener);
    }
});

contextBridge.exposeInMainWorld('vasio', {
    getState: () => ipcRenderer.invoke('api:get-state'),
    getDrivers: () => ipcRenderer.invoke('api:get-drivers'),
    getApplicationProfiles: () => ipcRenderer.invoke('api:get-application-profiles'),
    replaceApplicationProfiles: profiles => ipcRenderer.invoke('api:replace-application-profiles', profiles),
    getDiagnostics: limit => ipcRenderer.invoke('api:get-diagnostics', limit),
    setDiagnosticLevel: level => ipcRenderer.invoke('api:set-diagnostic-level', level),
    httpRequest: (method, path, body) => ipcRenderer.invoke('api:http-request', method, path, body),
    executeCommand: command => ipcRenderer.invoke('api:execute-command', command),
    startCorrelation: stereoPairId => ipcRenderer.invoke('api:execute-command', {
        id: `correlation-start-${Date.now()}`, command: 'audio.correlation.start', payload: { stereoPairId }
    }),
    stopCorrelation: () => ipcRenderer.invoke('api:execute-command', {
        id: `correlation-stop-${Date.now()}`, command: 'audio.correlation.stop'
    }),
    startAudio: () => ipcRenderer.invoke('api:execute-command', {
        id: `engine-start-${Date.now()}`, command: 'engine.start'
    }),
    stopAudio: () => ipcRenderer.invoke('api:execute-command', {
        id: `engine-stop-${Date.now()}`, command: 'engine.stop'
    }),
    applyConfiguration: configuration => ipcRenderer.invoke('api:configuration-apply', configuration),
    subscribeEvents: callback => {
        const listener = (_event, payload) => callback(payload);
        ipcRenderer.on('api:event', listener);
        return () => ipcRenderer.removeListener('api:event', listener);
    },
    startEngine: () => ipcRenderer.invoke('engine:start'),
    stopEngine: () => ipcRenderer.invoke('engine:stop'),
    onDisconnect: callback => {
        const listener = () => callback();
        ipcRenderer.on('api:disconnect', listener);
        return () => ipcRenderer.removeListener('api:disconnect', listener);
    }
});
