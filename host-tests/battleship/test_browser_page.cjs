const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('../../src/apps_local/battleship/web/BattleshipPage.html', 'utf8');
function element() { return {textContent:'', innerHTML:'', value:'0', disabled:true, className:'', hidden:false, children:[], appendChild(child) { this.children.push(child); }, setAttribute() {}}; }
const ids = ['status','phase','turn','profile','placement','feedback','hair','eyes','mouth','ship','grid','save','randomize','ready','battle','target','own','rematch','avatar','playerName','hudName','hud','setupState','hudTurn','ownSummary','enemySummary','resultBanner'];
const elements = Object.fromEntries(ids.map(id => [id,element()]));
const cols=[element(),element(),element()],rows=[element(),element(),element()];
const sockets = [], events = {}, sent = [], store = new Map();
let reconnect, now=1000;
class Socket {
  static OPEN = 1;
  constructor(url) { assert.equal(url,'ws://crossplay.local:81/battleship'); this.readyState = 1; sockets.push(this); }
  send(data) { sent.push(JSON.parse(data)); }
  close() { this.readyState = 3; if(this.onclose) this.onclose(); }
}
const token = '0123456789abcdef0123456789abcdef';
const resume = 'fedcba9876543210fedcba9876543210';
const sessionStorage = { getItem:key=>store.has(key)?store.get(key):null, setItem:(key,value)=>store.set(key,value), removeItem:key=>store.delete(key) };
const avatarSvg={base:'<svg></svg>',hair:{BALD:'<svg></svg>'},eyes:{GRIM:'<svg></svg>',SHADES:'<svg></svg>'},mouth:{GRIN:'<svg></svg>'}};
vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], {
  avatarSvg,
  document: {getElementById:id=>elements[id], createElement:element, querySelectorAll:q=>q==='.col-labels'?cols:rows},
  fetch: async (path,options) => { assert.equal(path,'/battleship/session'); assert.equal(options.cache,'no-store'); return {ok:true,text:async()=>token}; },
  location:{hostname:'crossplay.local'}, WebSocket:Socket, sessionStorage,
  window:{addEventListener:(name,fn)=>events[name]=fn}, Uint8Array,
  Date:{now:()=>now},
  setTimeout:fn=>{reconnect=fn;return 1;}, clearTimeout:()=>{reconnect=null;}
});
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const emptyState=(phase,myTurn=false,winner=-1,sunk=0)=>({type:'state',phase,myTurn,w:winner,b:'A'.repeat(34),i:'A'.repeat(18),s:sunk});
(async()=>{
  await flush();
  const socket=sockets[0];socket.onopen();
  assert.equal(elements.hair.children.length,14);
  assert.equal(elements.grid.children.length,100);
  assert.equal(elements.target.children.length,100);
  assert.equal(elements.own.children.length,100);
  assert.equal(cols[0].children.length,10);
  assert.equal(rows[0].children.length,10);
  assert.equal(elements.profile.disabled,false);
  assert.match(elements.avatar.innerHTML,/svg/);

  elements.save.onclick();
  assert.deepEqual(sent[0],['profile',token,0,0,0,0]);
  const view={type:'placement',accepted:true,profile:true,ready:false,revision:1,slots:[0,13,0],ships:Array.from({length:5},()=>[255,1])};
  socket.onmessage({data:JSON.stringify(view)});
  socket.onmessage({data:JSON.stringify({type:'resume',token:resume})});

  elements.randomize.onclick();
  assert.deepEqual(sent[1],['randomize',token,1]);
  const randomized={...view,revision:2,ships:[[0,1],[10,1],[20,1],[30,1],[40,1]]};
  socket.onmessage({data:JSON.stringify(randomized)});

  // First tap selects an existing ship; a second quick tap rotates it.
  now=2000;elements.grid.children[0].onclick();
  assert.equal(sent.length,2);
  now=2200;elements.grid.children[0].onclick();
  assert.deepEqual(sent[2],['place',token,2,0,0,0]);
  const rotated={...randomized,revision:3,ships:[[0,0],[10,1],[20,1],[30,1],[40,1]]};
  socket.onmessage({data:JSON.stringify(rotated)});

  elements.ready.onclick();
  assert.deepEqual(sent[3],['ready',token,3]);
  const ready={...rotated,ready:true,revision:4};
  socket.onmessage({data:JSON.stringify(ready)});
  assert.equal(store.size,1);

  socket.onmessage({data:JSON.stringify(emptyState('playing',true))});
  assert.equal(elements.profile.hidden,true);
  assert.equal(elements.placement.hidden,true);
  assert.equal(elements.hud.hidden,false);
  elements.target.children[12].onclick();
  assert.deepEqual(sent[4],['fire',token,4,12]);
  const fireReply={...ready,revision:5};
  socket.onmessage({data:JSON.stringify(fireReply)});
  socket.onmessage({data:JSON.stringify(emptyState('playing',false))});

  socket.close();reconnect();await flush();
  assert.equal(sockets.length,2);
  const resumed=sockets[1];resumed.onopen();
  assert.deepEqual(sent[5],['resume',token,5,resume]);
  resumed.onmessage({data:JSON.stringify(fireReply)});
  resumed.onmessage({data:JSON.stringify({type:'resume',token:resume})});
  resumed.onmessage({data:JSON.stringify(emptyState('finished',false,1,3))});
  assert.equal(elements.resultBanner.textContent,'VICTORY');
  assert.match(elements.resultBanner.className,/show/);
  assert.equal(elements.rematch.hidden,false);
  elements.rematch.onclick();
  assert.deepEqual(sent[6],['rematch',token,5]);
  resumed.onmessage({data:JSON.stringify(emptyState('placement'))});
  const replay={...view,revision:6,slots:ready.slots};
  resumed.onmessage({data:JSON.stringify(replay)});
  assert.equal(store.size,0);

  events.pagehide();assert.equal(reconnect,null);
  events.pageshow({persisted:true});await flush();
  assert.equal(sockets.length,3);
  console.log('Browser page: avatar, clean boards, randomize, double-tap rotate, battle HUD, result banner, secure resume and rematch passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
