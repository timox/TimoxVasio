const releaseInfo = {
    product: { name: 'TimoxVSSC', version: '0.2.0-alpha.5' },
    apiVersion: '1.1',
    components: [
        { id: 'gui', label: 'Interface GUI', version: '0.2.0-alpha.5', revision: null, description: 'Interface Electron/React' },
        { id: 'engine', label: 'Moteur audio', version: '1.3.0-alpha.2', revision: '8350d1d25c841cc7d5ee3239d345ca98b7bb8cf7', description: 'TimoxVAudioEngine' },
        { id: 'driver', label: 'Driver ASIO', version: '1.2.7-test', revision: 'd263c9470dd052af3bc0f0b43bf1b9611012171f', description: 'TimoxVasio préversion' },
        { id: 'pwar', label: 'Pont PWAR', version: '0.1.0-alpha.1', revision: '7179c868e7110ae90fbe5bfd24eb1f0f56f154e0', description: 'Client audio réseau Windows' }
    ]
};

export default releaseInfo;
