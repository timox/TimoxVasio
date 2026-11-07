import releaseInfo from './release-info';
import packageInfo from '../package.json';
import lock from '../artifacts.lock.json';

test('about information matches the packaged release pins', () => {
    expect(releaseInfo.product.version).toBe(packageInfo.version);
    expect(releaseInfo.apiVersion).toBe(lock.requiredApiVersion);
    expect(releaseInfo.components.find(item => item.id === 'engine').version).toBe(lock.release.engine.version);
    expect(releaseInfo.components.find(item => item.id === 'driver').version).toBe(lock.release.driver.version);
    expect(releaseInfo.components.find(item => item.id === 'pwar').version).toBe(lock.release.pwar.version);
});
