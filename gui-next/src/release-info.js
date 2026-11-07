const releaseInfo = {
    product: { name: 'TimoxVasio', version: '1.3.0-alpha.1' },
    apiVersion: '1.1',
    components: [
        { id: 'gui', label: 'Interface GUI', version: '1.3.0-alpha.1', revision: null, description: 'Interface Electron/React' },
        { id: 'engine', label: 'Moteur audio', version: '1.3.0-alpha.2', revision: null, description: 'Moteur et API' },
        { id: 'driver', label: 'Driver ASIO', version: '1.2.7-test', revision: null, description: 'Pilote TimoxVasio' },
        { id: 'pwar', label: 'Pont PWAR', version: '0.1.0-alpha.1', revision: null, description: 'Client audio réseau Windows' }
    ]
};

export default releaseInfo;
