const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('../../src/apps_local/battleship/web/BattleshipPage.html', 'utf8');
const elements = Object.fromEntries(['status','phase','turn'].map(id => [id,{textContent:''}]));
const sockets = [], events = {};
let reconnect;
class Socket {
  constructor(url) { assert.equal(url,'ws://crossplay.local:81/battleship'); sockets.push(this); }
  close() { if(this.onclose) this.onclose(); }
}
vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], {
  document: {getElementById: id => elements[id]},
  location: {hostname: 'crossplay.local'}, WebSocket: Socket,
  window: {addEventListener: (name,fn) => events[name]=fn},
  setTimeout: fn => { reconnect=fn; return 1; }, clearTimeout: () => { reconnect=null; }
});
const socket=sockets[0];
for (const phase of ['waiting','placement','playing','finished']) {
  socket.onmessage({data: JSON.stringify({type:'state',phase,connected:true,myTurn:true})});
  assert.equal(elements.phase.textContent,phase);
  assert.equal(elements.status.textContent,'Connected to X4 Pro');
}
socket.onmessage({data:'invalid'});
assert.equal(elements.phase.textContent,'finished');
socket.onclose();
assert.equal(elements.phase.textContent,'offline');
assert.equal(elements.turn.textContent,'');
reconnect();
assert.equal(sockets.length,2);
events.pagehide();
assert.equal(reconnect,null);
events.pageshow({persisted:true});
assert.equal(sockets.length,3);
console.log('Browser page: phases, malformed state, disconnect, reconnect and navigation passed');
