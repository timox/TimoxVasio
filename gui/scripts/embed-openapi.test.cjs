const test = require('node:test');
const assert = require('node:assert/strict');
const spec = require('../src/openapi-spec.generated.js');

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
