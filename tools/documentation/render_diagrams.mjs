// Render the Mermaid fences in docs/ using local Mermaid and Chromium.
// Setup and review instructions: docs/development/documentation.md.
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const options = {
  runtime: 'artifacts/documentation/mermaid-runtime',
  output: 'artifacts/documentation/mermaid',
  browser: process.env.CHROME_PATH,
};
for (let i = 2; i < process.argv.length; i += 2) {
  const key = process.argv[i].replace(/^--/, '');
  if (!(key in options) || !process.argv[i + 1]) {
    throw new Error('Usage: node render_diagrams.mjs --browser <Chromium executable> [--runtime <directory>] [--output <directory>]');
  }
  options[key] = process.argv[i + 1];
}
if (!options.browser) throw new Error('Provide --browser or CHROME_PATH.');
const runtime = path.resolve(root, options.runtime);
const output = path.resolve(root, options.output);
const require = createRequire(path.join(runtime, 'package.json'));
const puppeteer = require('puppeteer-core');
const mermaidPackage = require.resolve('mermaid/package.json');
const mermaidDist = path.join(path.dirname(mermaidPackage), 'dist');
const mermaidVersion = JSON.parse(await fs.readFile(mermaidPackage, 'utf8')).version;
await fs.mkdir(output, { recursive: true });

async function markdownFiles(directory) {
  const result = [];
  for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
    const filename = path.join(directory, entry.name);
    if (entry.isDirectory()) result.push(...await markdownFiles(filename));
    else if (entry.isFile() && entry.name.endsWith('.md')) result.push(filename);
  }
  return result.sort();
}

const diagrams = [];
for (const filename of await markdownFiles(path.join(root, 'docs'))) {
  const markdown = await fs.readFile(filename, 'utf8');
  // Honor other code fences so quoted Markdown examples are not rendered.
  const lines = markdown.split(/\r?\n/);
  let fence = null;
  let number = 0;
  for (let i = 0; i < lines.length; i++) {
    if (fence) {
      const close = lines[i].match(/^ {0,3}(`{3,}|~{3,})\s*$/);
      if (close && close[1][0] === fence.marker[0] && close[1].length >= fence.marker.length) {
        if (fence.mermaid) {
          const source = path.relative(root, filename).replaceAll('\\', '/');
          diagrams.push({
            source, line: fence.start + 1,
            id: source.replace(/^docs\//, '').replace(/\.md$/, '').replaceAll('/', '--') + `-${++number}`,
            code: lines.slice(fence.start + 1, i).join('\n'),
          });
        }
        fence = null;
      }
    } else {
      const open = lines[i].match(/^ {0,3}(`{3,}|~{3,})(.*)$/);
      if (open) fence = { marker: open[1], mermaid: open[2].trim() === 'mermaid', start: i };
    }
  }
  if (fence?.mermaid) throw new Error(`Unclosed Mermaid fence: ${filename}`);
}
if (!diagrams.length) throw new Error('No Mermaid diagrams found in docs/.');

const server = http.createServer(async (request, response) => {
  try {
    const url = new URL(request.url, 'http://localhost');
    if (url.pathname === '/') {
      response.setHeader('Content-Type', 'text/html; charset=utf-8');
      response.end('<!doctype html><html lang="en"><meta charset="utf-8"><title>Local Mermaid rendering</title><body><div id="diagram"></div></body></html>');
      return;
    }
    const filename = path.resolve(mermaidDist, '.' + decodeURIComponent(url.pathname));
    const relative = path.relative(mermaidDist, filename);
    if (relative.startsWith('..') || path.isAbsolute(relative)) {
      response.writeHead(403).end();
      return;
    }
    const data = await fs.readFile(filename);
    response.setHeader('Content-Type', /\.m?js$/.test(filename) ? 'text/javascript' : 'application/octet-stream');
    response.end(data);
  } catch {
    response.writeHead(404).end();
  }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
let browser;
const results = [];
try {
  browser = await puppeteer.launch({
    executablePath: path.resolve(options.browser), headless: true,
    userDataDir: path.join(output, 'browser-profile'),
    args: ['--disable-background-networking', '--no-first-run'],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 1000, height: 900, deviceScaleFactor: 1.5 });
  await page.setRequestInterception(true);
  page.on('request', request => {
    if (request.url().startsWith(origin + '/') || request.url().startsWith('data:')) request.continue();
    else request.abort();
  });
  await page.goto(origin);
  await page.evaluate(async () => {
    window.mermaid = (await import('/mermaid.esm.min.mjs')).default;
  });
  for (const diagram of diagrams) {
    await fs.writeFile(path.join(output, `${diagram.id}.mmd`), diagram.code + '\n');
    for (const theme of ['default', 'dark']) {
      const name = `${diagram.id}-${theme}`;
      const rendered = await page.evaluate(async ({ code, theme }) => {
        document.body.style.cssText = `margin:0;background:${theme === 'dark' ? '#0d1117' : '#ffffff'};`;
        const container = document.getElementById('diagram');
        container.innerHTML = '';
        container.style.cssText = 'box-sizing:content-box;width:900px;padding:24px;font-family:Arial,sans-serif;';
        window.mermaid.initialize({ startOnLoad: false, securityLevel: 'strict', theme });
        const { svg } = await window.mermaid.render('rendered-diagram', code);
        container.innerHTML = svg;
        await document.fonts.ready;
        const element = container.querySelector('svg');
        const bounds = element.getBoundingClientRect();
        const view = element.viewBox.baseVal;
        const graphics = element.getBBox();
        const clipped = graphics.x < view.x - 2 || graphics.y < view.y - 2
          || graphics.x + graphics.width > view.x + view.width + 2
          || graphics.y + graphics.height > view.y + view.height + 2;
        return {
          svg: element.outerHTML,
          width: Math.ceil(bounds.width), height: Math.ceil(bounds.height),
          scale: bounds.width / view.width, clipped,
        };
      }, { code: diagram.code, theme });
      await fs.writeFile(path.join(output, `${name}.svg`), rendered.svg);
      await (await page.$('#diagram')).screenshot({ path: path.join(output, `${name}.png`) });
      const { svg, ...metrics } = rendered;
      results.push({
        source: diagram.source, line: diagram.line, name, theme, ...metrics,
        sha256: createHash('sha256').update(diagram.code).digest('hex'),
      });
      console.log(`${name}: ${metrics.width}x${metrics.height}, scale=${metrics.scale.toFixed(2)}${metrics.clipped ? ', CLIPPED' : ''}`);
    }
  }
} finally {
  if (browser) await browser.close();
  await new Promise(resolve => server.close(resolve));
}

const escape = value => value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('"', '&quot;');
const cards = diagrams.map(diagram => `<section><h2><a href="${escape(pathToFileURL(path.join(root, diagram.source)).href)}">${escape(diagram.source)}:${diagram.line}</a></h2><div class="pair">${['default', 'dark'].map(theme => `<figure><a href="${diagram.id}-${theme}.svg"><img src="${diagram.id}-${theme}.png" alt="${escape(diagram.source)} diagram, ${theme} theme"></a><figcaption>${theme} (<a href="${diagram.id}-${theme}.svg">SVG</a>)</figcaption></figure>`).join('')}</div></section>`).join('\n');
await fs.writeFile(path.join(output, 'index.html'), `<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>A3D diagram review</title>
<style>body{font:16px Arial,sans-serif;margin:24px;background:#f6f8fa;color:#1f2328}h2{font-size:18px}.pair{display:flex;gap:12px}figure{margin:0;flex:1;min-width:0}img{width:100%;height:auto}section{margin:32px 0}a{color:#0969da}@media(max-width:900px){.pair{display:block}}</style>
<h1>A3D diagram review</h1><p>${diagrams.length} diagrams rendered locally with Mermaid ${mermaidVersion}. Click an image for its SVG. Inspect labels, arrows, clipping and reading order; successful rendering alone does not establish correctness.</p>${cards}</html>`);
await fs.writeFile(path.join(output, 'manifest.json'), JSON.stringify({
  mermaidVersion, diagrams: diagrams.length, renders: results,
}, null, 2) + '\n');
const failures = results.filter(result => result.clipped || result.scale < 0.75);
console.log(`Rendered ${diagrams.length} diagrams in two themes. Gallery: ${path.join(output, 'index.html')}`);
if (failures.length) {
  console.error('Review clipping or small text:', failures.map(result => result.name).join(', '));
  process.exitCode = 1;
}
