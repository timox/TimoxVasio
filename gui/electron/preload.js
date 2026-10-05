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
