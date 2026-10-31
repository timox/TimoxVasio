const { EventEmitter } = require('node:events');
const { randomUUID } = require('node:crypto');
const { fetchJson } = require('./http-json');

class ApiClient extends EventEmitter {
    constructor({ baseUrl, webSocketUrl, WebSocket: WebSocketImpl, timeoutMs = 10000 }) {
        super();
        if (!baseUrl || !webSocketUrl || !WebSocketImpl) throw new TypeError('API URLs and WebSocket implementation are required');
        this.baseUrl = baseUrl.replace(/\/$/, '');
        this.webSocketUrl = webSocketUrl;
        this.WebSocket = WebSocketImpl;
        this.socket = null;
        this.pending = new Map();
        this.timeoutMs = timeoutMs;
    }

    async getState() {
        return this.getJson('/api/v1/state');
    }

    async getDrivers() {
        return this.getJson('/api/v1/drivers');
    }

    async getWasapiDevices() {
        return this.getJson('/api/v1/wasapi-devices');
    }

    async getWindowsAudioApplications() {
        return this.getJson('/api/v1/windows-audio-applications');
    }

    async getApplicationProfiles() {
        return this.getJson('/api/v1/application-profiles');
    }

    async getProcessCopyTemplates() {
        return this.getJson('/api/v1/process-copy-templates');
    }

    async replaceProcessCopyTemplates(templates) {
        return this.requestJson('/api/v1/process-copy-templates', {
            method: 'PUT', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ templates })
        });
    }

    async getDiagnostics(limit = 200) {
        return this.getJson(`/api/v1/diagnostics?limit=${encodeURIComponent(limit)}`);
    }

    async setDiagnosticLevel(level) {
        return this.requestJson('/api/v1/diagnostics', {
            method: 'PUT',
            headers: { 'content-type': 'application/json' },
            body: JSON.stringify({ level })
        });
    }

    async replaceApplicationProfiles(profiles) {
        return this.requestJson('/api/v1/application-profiles', {
            method: 'PUT',
            headers: { 'content-type': 'application/json' },
            body: JSON.stringify({ profiles })
        });
    }

    async getJson(path) {
        return this.requestJson(path);
    }

    async requestJson(path, options) {
        const { response, body } = await fetchJson(`${this.baseUrl}${path}`, options, fetch, this.timeoutMs);
        if (!response.ok) {
            const details = body.error || body;
            const error = new Error(details.message || `VASIO API ${path}: HTTP ${response.status}`);
            Object.assign(error, details, { status: response.status });
            throw error;
        }
        return body;
    }

    connect() {
        if (this.socket && this.socket.readyState === this.WebSocket.OPEN) return Promise.resolve();
        if (this.connecting) return this.connecting;
        this.socket = new this.WebSocket(this.webSocketUrl, 'vasio.api.v1');
        this.connecting = new Promise((resolve, reject) => {
            const socket = this.socket;
            const cleanup = () => { clearTimeout(timer); socket.off('open', onOpen); socket.off('error', onInitialError); socket.off('close', onInitialClose); };
            const onOpen = () => { cleanup(); this.connecting = null; resolve(); };
            const onInitialError = error => { cleanup(); this.connecting = null; reject(error); };
            const onInitialClose = () => onInitialError(new Error('WebSocket closed before connection'));
            const timer = setTimeout(() => {
                onInitialError(new Error('Engine WebSocket connection timed out'));
                socket.terminate();
            }, this.timeoutMs);
            socket.once('open', onOpen);
            socket.once('error', onInitialError);
            socket.once('close', onInitialClose);
            socket.on('error', error => this.rejectPending(error));
            socket.on('message', data => this.handleMessage(data));
            socket.on('close', () => {
                if (this.socket === socket) this.socket = null;
                this.connecting = null;
                this.rejectPending(new Error('WebSocket disconnected'));
                this.emit('disconnect');
            });
        });
        return this.connecting;
    }

    applyConfiguration(configuration, id = randomUUID()) {
        return this.sendCommand({ id, command: 'configuration.apply', payload: configuration });
    }

    stopEngine(id = randomUUID()) {
        return this.sendCommand({ id, command: 'engine.stop' });
    }

    async httpRequest(method, path, body) {
        const routes = ['/api/v1/state', '/api/v1/drivers', '/api/v1/wasapi-devices', '/api/v1/windows-audio-applications', '/api/v1/openapi.json',
            '/api/v1/schemas/api-v1.json', '/api/v1/diagnostics', '/api/v1/application-profiles', '/api/v1/process-copy-templates', '/api/v1/pwar', '/api/v1/pwar/client'];
        const documented = routes.includes(path) ||
            (typeof path === 'string' && /^\/api\/v1\/diagnostics\?limit=\d+$/.test(path));
        if (!['GET', 'PUT'].includes(method) || !documented)
            throw new TypeError('Request is outside the documented TimoxVasio HTTP routes');
        const { response, body: responseBody } = await fetchJson(`${this.baseUrl}${path}`, {
            method,
            headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
            body: body === undefined ? undefined : JSON.stringify(body)
        }, fetch, this.timeoutMs);
        return { status: response.status, body: responseBody };
    }

    sendCommand(command) {
        if (!this.socket || this.socket.readyState !== this.WebSocket.OPEN)
            return Promise.reject(new Error('WebSocket disconnected'));
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => { this.pending.delete(command.id); reject(new Error(`Engine command ${command.command} timed out`)); }, this.timeoutMs);
            this.pending.set(command.id, { resolve, reject, timer });
            this.socket.send(JSON.stringify(command), error => {
                if (!error) return;
                clearTimeout(timer);
                this.pending.delete(command.id);
                reject(error);
            });
        });
    }

    handleMessage(raw) {
        let message;
        try { message = JSON.parse(raw.toString()); }
        catch (error) { this.emit('protocolError', error); return; }
        if (message.event) {
            this.emit('event', message);
            return;
        }
        if (!message.id || !this.pending.has(message.id)) return;
        const pending = this.pending.get(message.id);
        this.pending.delete(message.id);
        clearTimeout(pending.timer);
        if (message.success) pending.resolve(message.result);
        else {
            const error = new Error(message.error?.message || 'VASIO API command failed');
            Object.assign(error, message.error || {});
            pending.reject(error);
        }
    }

    rejectPending(error) {
        for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(error); }
        this.pending.clear();
    }

    close() {
        if (!this.socket) return;
        this.rejectPending(new Error('WebSocket closed'));
        const socket = this.socket;
        this.socket = null;
        socket.close();
    }
}

module.exports = { ApiClient };
