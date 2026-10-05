const fs = require('node:fs');
const path = require('node:path');
const swaggerUi = require.resolve('swagger-ui-dist/swagger-ui-bundle.js');
const swaggerCss = require.resolve('swagger-ui-dist/swagger-ui.css');

const guiRoot = path.resolve(__dirname, '..');
const document = JSON.parse(fs.readFileSync(path.resolve(guiRoot, '../openapi-v1.json'), 'utf8'));
const schemas = JSON.parse(fs.readFileSync(path.resolve(guiRoot, '../schemas/api-v1.json'), 'utf8'));

function resolveSchema(value, stack = new Set()) {
    if (Array.isArray(value)) return value.map(item => resolveSchema(item, stack));
    if (!value || typeof value !== 'object') return value;
    if (typeof value.$ref === 'string' && value.$ref.startsWith('#/definitions/')) {
        const name = value.$ref.slice('#/definitions/'.length);
        if (stack.has(name)) return {};
        return resolveSchema(schemas.definitions[name], new Set(stack).add(name));
    }
    return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, resolveSchema(item, stack)]));
}

function embed(value) {
    if (Array.isArray(value)) return value.map(embed);
    if (!value || typeof value !== 'object') return value;
    if (typeof value.$ref === 'string' && value.$ref.startsWith('./schemas/api-v1.json#/definitions/')) {
        return resolveSchema(schemas.definitions[value.$ref.slice('./schemas/api-v1.json#/definitions/'.length)]);
    }
    return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, embed(item)]));
}

const output = `// Generated from ../openapi-v1.json and ../schemas/api-v1.json. Do not edit.\nmodule.exports = ${JSON.stringify(embed(document))};\n`;
fs.writeFileSync(path.join(guiRoot, 'src/openapi-spec.generated.js'), output);
const publicRoot = path.join(guiRoot, 'public');
fs.mkdirSync(publicRoot, { recursive: true });
fs.copyFileSync(swaggerUi, path.join(publicRoot, 'swagger-ui-bundle.js'));
fs.copyFileSync(swaggerCss, path.join(publicRoot, 'swagger-ui.css'));
const spec = JSON.stringify(embed(document)).replace(/</g, '\\u003c');
const html = `<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>TimoxVasio API</title><link rel="stylesheet" href="swagger-ui.css"></head><body><div id="swagger-ui"></div><script src="swagger-ui-bundle.js"></script><script>window.onload=()=>SwaggerUIBundle({spec:${spec},dom_id:'#swagger-ui',deepLinking:true,docExpansion:'list',defaultModelsExpandDepth:-1});</script></body></html>`;
fs.writeFileSync(path.join(publicRoot, 'swagger.html'), html);
