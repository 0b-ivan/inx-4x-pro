const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('../../src/apps_local/battleship/web/BattleshipPage.html', 'utf8');
function element() { return {textContent:'', value:'0', disabled:true, className:'', hidden:false, children:[], appendChild(child) { this.children.push(child); }}; }
const ids = ['status','phase','turn','profile','placement','feedback','hair','eyes','mouth','ship','direction','grid','save','ready','battle','target','own','rematch'];
const elements = Object.fromEntries(ids.map(id => [id,element()]));
elements.direction.value = '1';
const sockets = [], events = {}, sent = [], store = new Map();
let reconnect;
class Socket {
  static OPEN = 1;
  constructor(url) { assert.equal(url,'ws://crossplay.local:81/battleship'); this.readyState = 1; sockets.push(this); }
  send(data) { sent.push(JSON.parse(data)); }
  close() { this.readyState = 3; if(this.onclose) this.onclose(); }
}
const token = '0123456789abcdef0123456789abcdef';
const resume = 'fedcba9876543210fedcba9876543210';
const sessionStorage = {
  getItem: key => store.has(key) ? store.get(key) : null,
  setItem: (key,value) => store.set(key,value),
  removeItem: key => store.delete(key)
};
vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], {
  document: {getElementById: id => elements[id], createElement: element},
  fetch: async (path,options) => { assert.equal(path,'/battleship/session'); assert.equal(options.cache,'no-store'); return {ok:true,text:async()=>token}; },
  location: {hostname: 'crossplay.local'}, WebSocket: Socket, sessionStorage,
  window: {addEventListener: (name,fn) => events[name]=fn},
  Uint8Array,
  setTimeout: fn => { reconnect=fn; return 1; }, clearTimeout: () => { reconnect=null; }
});
const flush = () => new Promise(resolve => setImmediate(resolve));
const emptyState = (phase,myTurn=false,winner=-1) => ({type:'state',phase,myTurn,w:winner,b:'A'.repeat(34),i:'A'.repeat(18)});
(async () => {
  await flush();
  const socket=sockets[0]; socket.onopen();
  assert.equal(elements.hair.children.length,14);
  assert.equal(elements.grid.children.length,100);
  assert.equal(elements.target.children.length,100);
  assert.equal(elements.own.children.length,100);
  assert.equal(elements.profile.disabled,false);

  elements.save.onclick();
  assert.deepEqual(sent[0],['profile',token,0,0,0,0]);
  const view = {type:'placement',accepted:true,profile:true,ready:false,revision:1,slots:[0,13,0],ships:Array.from({length:5},()=>[255,1])};
  socket.onmessage({data: JSON.stringify(view)});
  socket.onmessage({data: JSON.stringify({type:'resume',token:resume})});

  elements.grid.children[22].onclick();
  assert.deepEqual(sent[1],['place',token,1,0,22,1]);
  const placed = {...view,revision:2,ships:[[0,1],[10,1],[20,1],[30,1],[40,1]]};
  socket.onmessage({data: JSON.stringify(placed)});
  elements.ready.onclick();
  assert.deepEqual(sent[2],['ready',token,2]);
  const ready = {...placed,ready:true,revision:3};
  socket.onmessage({data: JSON.stringify(ready)});
  assert.equal(store.size,1);

  socket.onmessage({data: JSON.stringify(emptyState('playing',true))});
  elements.target.children[12].onclick();
  assert.deepEqual(sent[3],['fire',token,3,12]);
  const fireReply = {...ready,revision:4};
  socket.onmessage({data: JSON.stringify(fireReply)});
  socket.onmessage({data: JSON.stringify(emptyState('playing',false))});

  socket.close();
  assert.equal(elements.phase.textContent,'offline');
  reconnect(); await flush();
  assert.equal(sockets.length,2);
  const resumed=sockets[1]; resumed.onopen();
  assert.deepEqual(sent[4],['resume',token,4,resume]);
  resumed.onmessage({data: JSON.stringify(fireReply)});
  resumed.onmessage({data: JSON.stringify({type:'resume',token:resume})});
  resumed.onmessage({data: JSON.stringify(emptyState('playing',false))});
  assert.equal(elements.feedback.textContent,'Waiting for the X4 Pro.');
  assert.equal(elements.battle.hidden,false);

  resumed.onmessage({data: JSON.stringify(emptyState('finished',false,1))});
  assert.equal(elements.rematch.hidden,false);
  elements.rematch.onclick();
  assert.deepEqual(sent[5],['rematch',token,4]);
  resumed.onmessage({data: JSON.stringify(emptyState('placement'))});
  const replay = {...view,revision:5,slots:ready.slots};
  resumed.onmessage({data: JSON.stringify(replay)});
  assert.equal(store.size,0); // rematch requires a fresh confirmed fleet before resume is persisted again

  events.pagehide();
  assert.equal(reconnect,null);
  events.pageshow({persisted:true}); await flush();
  assert.equal(sockets.length,3);
  console.log('Browser page: profile, placement, live board, fire, secure resume, rematch and reconnect passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
