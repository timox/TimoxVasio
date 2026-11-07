const { mergeConfigurationDraft } = require('./configuration-merge');

const route = (id, source) => ({ id, sourceEndpointId: source,
    destinationEndpointId: 'physical:card:output:1', gainDb: 0, mute: false });
const config = routes => ({ physicalDriverId: 'card', sampleRate: 48000, bufferFrames: 512, routes });

test('bridge edits merge by ID and defaults while preserving external additions', () => {
    const mic = { id: 'mic', kind: 'capture', defaultRole: 'console' };
    const base = { ...config([]), wasapiBridges: [mic] };
    const latest = { ...base, wasapiBridges: [{ ...mic, enabled: true, bufferDuration100ns: 0 }, { id: 'out', kind: 'render', endpointId: 'speaker' }] };
    expect(mergeConfigurationDraft(base, { ...base, wasapiBridges: [{ ...mic, enabled: false }] }, latest).wasapiBridges)
        .toEqual([latest.wasapiBridges[1], { ...mic, enabled: false }]);
    expect(mergeConfigurationDraft(base, { ...base, wasapiBridges: [] }, latest).wasapiBridges).toEqual([latest.wasapiBridges[1]]);
    expect(mergeConfigurationDraft(config([]), config([]), latest).wasapiBridges).toEqual(latest.wasapiBridges);
    expect(mergeConfigurationDraft(base, base, config([]))).not.toHaveProperty('wasapiBridges');
    expect(() => mergeConfigurationDraft(base, { ...base, wasapiBridges: [] }, { ...base, wasapiBridges: [{ ...mic, enabled: false }] })).toThrow('changed in another application');
});

test('preserves a route added by Mixette while Control adds a different route', () => {
    const base = config([route('shared', 'virtual:sc:output:1')]);
    const draft = config([...base.routes, route('control', 'virtual:traktor:output:1')]);
    const latest = config([...base.routes, route('mixette', 'virtual:mixette:output:1')]);
    expect(mergeConfigurationDraft(base, draft, latest).routes.map(item => item.id))
        .toEqual(['shared', 'mixette', 'control']);
});

test('preserves unrelated external routes when Control removes its own connection', () => {
    const base = config([route('control', 'virtual:traktor:output:1')]);
    const latest = config([...base.routes, route('mixette', 'virtual:mixette:output:1')]);
    expect(mergeConfigurationDraft(base, config([]), latest).routes.map(item => item.id))
        .toEqual(['mixette']);
});

test('rejects competing edits of the same connection', () => {
    const original = route('shared', 'virtual:sc:output:1');
    expect(() => mergeConfigurationDraft(config([original]),
        config([{ ...original, gainDb: -6 }]),
        config([{ ...original, gainDb: 3 }]))).toThrow('changed in another application');
});
test('clock source changes are merged by value, not object identity', () => {
    const base = { physicalDriverId: null, sampleRate: 48000, bufferFrames: 256, routes: [], clockSource: { kind: 'asio' } };
    const draft = { ...base, clockSource: { kind: 'software' } };
    expect(mergeConfigurationDraft(base, draft, { ...base, clockSource: { kind: 'asio' } }).clockSource).toEqual({ kind: 'software' });
    expect(mergeConfigurationDraft(base, { ...base, clockSource: { kind: 'asio' } }, draft).clockSource).toEqual({ kind: 'software' });
});
