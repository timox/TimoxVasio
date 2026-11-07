const test = require('node:test');
const assert = require('node:assert/strict');
const spec = require('../src/openapi-spec.generated.js');

test('Swagger exposes supervision models and preserves their descriptions', () => {
  for (const name of ['Supervision', 'SupervisionEngine', 'Adherent', 'AdherentIdentity',
    'AdherentCapabilities', 'ConnectionNode', 'EngineLink', 'EngineChannelMeasurement',
    'ClientObservation', 'SupervisionObservations', 'WasapiBridgeObservation']) {
    assert.ok(spec.components.schemas[name].description, name);
    assert.equal(spec.components.schemas[name].allOf[0].type, 'object', name);
  }
  assert.equal(spec.components.schemas.EngineLink.allOf[0].properties.owner.const, 'engine');
  assert.ok(spec.paths['/api/v1/ws'].get['x-websocket'].events.includes('supervision.observations'));
  const html = require('node:fs').readFileSync(require('node:path').join(__dirname, '../public/swagger.html'), 'utf8');
  assert.ok(html.includes('defaultModelsExpandDepth:1'));
});

test('the bundled Swagger document has no references to resolve from file://', () => {
  const refs = [];
  function visit(value, path = '') {
    if (!value || typeof value !== 'object') return;
    if (Array.isArray(value)) {
      value.forEach((item, index) => visit(item, `${path}/${index}`));
      return;
    }
    if (typeof value.$ref === 'string') refs.push(`${path}: ${value.$ref}`);
    Object.entries(value).forEach(([key, item]) => visit(item, `${path}/${key}`));
  }
  visit(spec);
  assert.deepEqual(refs, []);
  assert.equal(spec.components.schemas.DiagnosticLevel.type, 'object');
  assert.equal(spec.components.schemas.Error.type, 'object');
});
