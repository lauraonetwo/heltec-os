// Exercise the actual embedded browser script without a browser or extra packages.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const header = fs.readFileSync(path.join(__dirname, '../firmware/HeltecOS/MessagesPage.h'), 'utf8');
const script = header.match(/<script nonce="\{\{NONCE\}\}">([\s\S]*?)<\/script>/)[1].replace('{{TOKEN}}', 'TESTTOKEN');
class Element {
  constructor() { this.children = []; this.handlers = {}; this.value = ''; this.textContent = ''; }
  replaceChildren() { this.children = []; }
  append(...children) { this.children.push(...children); }
  addEventListener(name, handler) { this.handlers[name] = handler; }
}
const elements = Object.fromEntries(['status', 'text', 'send', 'compose', 'history', 'stop'].map(id => [id, new Element()]));
const calls = [];
let state = { busy: false, messages: [{ id: 1, phone: false, text: 'SOS' }] };
let writeStatus = 201;
let readStatus = 200;
const context = {
  document: { getElementById: id => elements[id], createElement: () => new Element() },
  location: { href: '' }, URLSearchParams, Date, Error,
  setTimeout: () => 0,
  fetch: async (url, options) => {
    calls.push({ url, options });
    const status = options.method === 'POST' ? writeStatus : readStatus;
    return { status, ok: status < 400, json: async () => state, text: async () => 'Light busy; wait or stop' };
  }
};
vm.createContext(context);
vm.runInContext(script, context);
async function main() {
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(elements.history.children[0].children[1].textContent, 'SOS');
  assert.equal(elements.send.disabled, false);
  elements.text.value = 'hello 123';
  await elements.compose.handlers.submit({ preventDefault() {} });
  const sent = calls.find(call => call.options.method === 'POST');
  assert.equal(sent.url, '/api/messages');
  assert.equal(sent.options.headers['X-Heltec-Token'], 'TESTTOKEN');
  assert.equal(sent.options.body.get('text'), 'hello 123');
  assert.equal(elements.text.value, '');
  elements.text.value = 'RETRY'; writeStatus = 409;
  await elements.compose.handlers.submit({ preventDefault() {} });
  assert.equal(elements.text.value, 'RETRY');
  assert.match(elements.status.textContent, /Light busy/);
  const count = calls.length;
  elements.text.value = '<script>';
  await elements.compose.handlers.submit({ preventDefault() {} });
  assert.equal(calls.length, count); // Invalid text stays local.
  state = { busy: true, messages: [{ id: 2, phone: true, text: 'HELLO' }] };
  await vm.runInContext('refresh()', context);
  assert.equal(elements.send.disabled, true);
  assert.match(elements.history.children[0].children[0].textContent, /Phone/);
  writeStatus = 200;
  await elements.stop.handlers.click();
  assert(calls.some(call => call.url === '/api/light/stop' && call.options.headers['X-Heltec-Token'] === 'TESTTOKEN'));
  readStatus = 401;
  await vm.runInContext('refresh()', context);
  assert.equal(context.location.href, '/messages');
  console.log('Embedded message page tests passed');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
