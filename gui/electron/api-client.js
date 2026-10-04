const { EventEmitter } = require('node:events');
const { randomUUID } = require('node:crypto');

class ApiClient extends EventEmitter {
    constructor({ baseUrl, webSocketUrl, WebSocket: WebSocketImpl }) {
        super();
        if (!baseUrl || !webSocketUrl || !WebSocketImpl) throw new TypeError('API URLs and WebSocket implementation are required');
        this.baseUrl = baseUrl.replace(/\/$/, '');
        this.webSocketUrl = webSocketUrl;
        this.WebSocket = WebSocketImpl;
        this.socket = null;
        this.pending = new Map();
    }

    async getState() {
        return this.getJson('/api/v1/state');
    }

    async getDrivers() {
        return this.getJson('/api/v1/drivers');
    }

    async getJson(path) {
        const response = await fetch(`${this.baseUrl}${path}`);
        if (!response.ok) throw new Error(`VASIO API ${path}: HTTP ${response.status}`);
        return response.json();
    }

    connect() {
        if (this.socket && this.socket.readyState === this.WebSocket.OPEN) return Promise.resolve();
        if (this.connecting) return this.connecting;
        this.socket = new this.WebSocket(this.webSocketUrl, 'vasio.api.v1');
        this.connecting = new Promise((resolve, reject) => {
            const socket = this.socket;
            const onOpen = () => {
                socket.off('error', onInitialError);
                this.connecting = null;
                resolve();
            };
            const onInitialError = error => {
                socket.off('open', onOpen);
                this.connecting = null;
                reject(error);
            };
            socket.once('open', onOpen);
            socket.once('error', onInitialError);
            socket.on('message', data => this.handleMessage(data));
            socket.on('close', () => {
                this.socket = null;
                this.connecting = null;
                this.rejectPending(new Error('WebSocket disconnected'));
                this.emit('disconnect');
            });
        });
        return this.connecting;
    }

    applyConfiguration(configuration, id = randomUUID()) {
        if (!this.socket || this.socket.readyState !== this.WebSocket.OPEN)
            return Promise.reject(new Error('WebSocket disconnected'));
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.socket.send(JSON.stringify({ id, command: 'configuration.apply', payload: configuration }), error => {
                if (!error) return;
                this.pending.delete(id);
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
        if (message.success) pending.resolve(message.result);
        else {
            const error = new Error(message.error?.message || 'VASIO API command failed');
            Object.assign(error, message.error || {});
            pending.reject(error);
        }
    }

    rejectPending(error) {
        for (const pending of this.pending.values()) pending.reject(error);
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
