const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('../../src/apps_local/battleship/web/BattleshipPage.html', 'utf8');
function element() { return {textContent:'', value:'0', disabled:true, className:'', children:[], appendChild(child) { this.children.push(child); }}; }
const elements = Object.fromEntries(['status','phase','turn','profile','placement','feedback','hair','eyes','mouth','ship','direction','grid','save','ready'].map(id => [id,element()]));
elements.direction.value = '1';
const sockets = [], events = {}, sent = [];
let reconnect;
class Socket {
  static OPEN = 1;
  constructor(url) { assert.equal(url,'ws://crossplay.local:81/battleship'); this.readyState = 1; sockets.push(this); }
  send(data) { sent.push(JSON.parse(data)); }
  close() { this.readyState = 3; if(this.onclose) this.onclose(); }
}
const token = '0123456789abcdef0123456789abcdef';
vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], {
  document: {getElementById: id => elements[id], createElement: element},
  fetch: async (path,options) => { assert.equal(path,'/battleship/session'); assert.equal(options.cache,'no-store'); return {ok:true,text:async()=>token}; },
  location: {hostname: 'crossplay.local'}, WebSocket: Socket,
  window: {addEventListener: (name,fn) => events[name]=fn},
  setTimeout: fn => { reconnect=fn; return 1; }, clearTimeout: () => { reconnect=null; }
});
const flush = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  await flush();
  const socket=sockets[0]; socket.onopen();
  assert.equal(elements.hair.children.length,14);
  assert.equal(elements.grid.children.length,100);
  assert.equal(elements.profile.disabled,false);
  assert.equal(elements.placement.disabled,true);
  elements.save.onclick();
  assert.deepEqual(sent[0],['profile',token,0,0,0,0]);
  elements.save.onclick(); assert.equal(sent.length,1); // one in flight
  const view = {type:'placement',accepted:true,profile:true,ready:false,revision:1,slots:[0,13,0],ships:Array.from({length:5},()=>[255,1])};
  socket.onmessage({data: JSON.stringify(view)});
  assert.equal(elements.placement.disabled,false);
  assert.equal(elements.ready.disabled,true);
  elements.grid.children[22].onclick();
  assert.deepEqual(sent[1],['place',token,1,0,22,1]);
  socket.onmessage({data: JSON.stringify({...view,revision:2,ships:[[0,1],[10,1],[20,1],[30,1],[40,1]]})});
  assert.equal(elements.ready.disabled,false);
  assert.equal(elements.grid.children[0].className,'ship');
  elements.ready.onclick(); assert.deepEqual(sent[2],['ready',token,2]);
  socket.onmessage({data: JSON.stringify({...view,ready:true,revision:3})});
  assert.equal(elements.placement.disabled,true);
  assert.equal(elements.profile.disabled,true);
  for (const phase of ['waiting','placement','playing','finished']) {
    socket.onmessage({data: JSON.stringify({type:'state',phase,connected:true,myTurn:true})});
    assert.equal(elements.phase.textContent,phase);
    assert.equal(elements.status.textContent,'Connected to X4 Pro');
  }
  for (const data of ['invalid','null','{}',JSON.stringify({...view,ships:[null]})]) socket.onmessage({data});
  assert.equal(elements.phase.textContent,'finished');
  socket.close();
  assert.equal(elements.phase.textContent,'offline');
  assert.equal(elements.placement.disabled,true);
  reconnect(); await flush();
  assert.equal(sockets.length,2);
  sockets[1].onopen();
  elements.save.onclick();
  assert.equal(sent.at(-1)[2],0); // fresh draft/session revision
  events.pagehide();
  assert.equal(reconnect,null);
  events.pageshow({persisted:true}); await flush();
  assert.equal(sockets.length,3);
  console.log('Browser page: profile, placement, single-flight, host-only grid, malformed replies and reconnect passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
