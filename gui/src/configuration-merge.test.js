const { mergeConfigurationDraft } = require('./configuration-merge');

const route = (id, source) => ({ id, sourceEndpointId: source,
    destinationEndpointId: 'physical:card:output:1', gainDb: 0, mute: false });
const config = routes => ({ physicalDriverId: 'card', sampleRate: 48000, bufferFrames: 512, routes });

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
