const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('../../src/apps_local/battleship/web/BattleshipPage.html', 'utf8');
function element() {
  const node = {
    textContent:'', value:'0', disabled:true, className:'', hidden:false, children:[], _innerHTML:'', dataset:{},
    appendChild(child) { this.children.push(child); },
    replaceChildren(...children) { this.children = children; },
    addEventListener(name, fn) { this[`on${name}`] = fn; },
    setAttribute() {},
    classList: { contains() { return false; } }
  };
  Object.defineProperty(node,'innerHTML',{get(){return this._innerHTML;},set(value){this._innerHTML=value;this.children=[];}});
  Object.defineProperty(node,'options',{get(){return this.children;}});
  return node;
}
const ids = [
  'app','status','phase','turn','profile','placement','feedback','hair','eyes','mouth','ship','grid','save','randomize','ready','battle','target','own','surrender','rematch','avatar','playerName','hudName','hud','setupState','hudTurn','ownSummary','enemySummary','enemyName','youPanel','enemyPanel','eventBanner','resultBanner',
  'devicePlayer','devicePlayerName','deviceCallsign','devicePlayerMode','deviceMetrics','deviceRadar','savedPlayer','useGuestPlayer','playerPin','loginPlayer','guestCallsignActions','registerName','registerPin','registerPlayer','playerMessage'
];
const elements = Object.fromEntries(ids.map(id => [id,element()]));
elements.app.classList = { contains: name => elements.app.className.split(/\s+/).includes(name) };
const guestButtons=[element(),element(),element()];
guestButtons.forEach((button,index)=>button.dataset.slot=String(index));
const cols=[element(),element()],rows=[element(),element()];
const sockets = [], events = {}, sent = [], store = new Map(), delayed=[];
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
const playerState = {
  ok:true,
  persistence:true,
  current:{name:'SPIKY WINK BEARD',callsign:'SPIKY WINK BEARD',registered:false,canRegister:false},
  profile:{xp:0,level:1,playerClass:'ALL-ROUNDER',radar:[0,0,0,0,0,0],battleship:{rank:'NO RANK',wins:0,losses:0,draws:0,bestStreak:0}},
  players:[]
};
vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], {
  avatarSvg,
  document: {
    getElementById:id=>elements[id],
    createElement:element,
    querySelectorAll:q=>q==='.col-labels'?cols:q==='#guestCallsignActions button'?guestButtons:rows
  },
  fetch: async (path,options={}) => {
    if(path==='/battleship/session') {
      assert.equal(options.cache,'no-store');
      return {ok:true,text:async()=>token};
    }
    if(path.startsWith('/player/state?token=')) return {ok:true,json:async()=>playerState};
    if(path==='/player/action') return {ok:true,json:async()=>playerState};
    throw new Error(`unexpected fetch ${path}`);
  },
  location:{hostname:'crossplay.local'}, WebSocket:Socket, sessionStorage,
  window:{addEventListener:(name,fn)=>events[name]=fn}, Uint8Array,
  Date:{now:()=>now},
  setTimeout:(fn,ms)=>{if(ms===2000){reconnect=fn;return 1;}if(ms>=1000){delayed.push(fn);return 2;}fn();return 3;},
  clearTimeout:()=>{reconnect=null;},
  setInterval:()=>1,
  encodeURIComponent
});
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const emptyState=(phase,myTurn=false,winner=-1,sunk=0,board='A'.repeat(34),incoming='A'.repeat(18))=>({type:'s',phase,myTurn,w:winner,b:board,i:incoming,s:sunk});
(async()=>{
  assert.match(html,/filter:brightness\(0\) invert\(1\)/);
  assert.match(html,/grid-template-columns:repeat\(3,minmax\(0,1fr\)\)/);
  assert.match(html,/side-panel\.active/);
  assert.ok(html.includes('touch-action:manipulation'));
  assert.ok(html.includes('body:has(main.in-battle)'));
  assert.ok(html.includes("window.addEventListener('contextmenu'"));
  assert.ok(html.includes("window.addEventListener('touchmove'"));
  assert.ok(html.includes("window.addEventListener('wheel'"));
  assert.match(html,/sunk-segment/);
  assert.match(html,/ship-sunk-flash/);
  assert.match(html,/d\.type==='peer'/);
  assert.match(html,/d\.type==='f'/);
  await flush();
  assert.equal(elements.devicePlayerName.textContent,'SPIKY WINK BEARD');
  const socket=sockets[0];socket.onopen();
  socket.onmessage({data:JSON.stringify({type:'peer',name:'SPIKY WINK BEARD'})});
  assert.equal(elements.enemyName.textContent,'SPIKY WINK BEARD');
  assert.equal(elements.hair.children.length,14);
  assert.equal(elements.grid.children.length,100);
  assert.equal(elements.target.children.length,100);
  assert.equal(elements.own.children.length,100);
  assert.equal(cols[0].children.length,10);
  assert.equal(rows[0].children.length,10);
  assert.equal(elements.profile.disabled,false);
  assert.equal(elements.placement.hidden,true);
  assert.match(elements.avatar.innerHTML,/svg/);

  elements.save.onclick();
  assert.deepEqual(sent[0],['profile',token,0,0,0,0]);
  const view={type:'placement',accepted:true,profile:true,ready:false,revision:1,slots:[0,13,0],ships:Array.from({length:5},()=>[255,1])};
  socket.onmessage({data:JSON.stringify(view)});
  socket.onmessage({data:JSON.stringify({type:'resume',token:resume})});
  assert.equal(elements.profile.hidden,true);
  assert.equal(elements.placement.hidden,false);

  elements.randomize.onclick();
  assert.deepEqual(sent[1],['randomize',token,1]);
  const randomized={...view,revision:2,ships:[[0,1],[10,1],[20,1],[30,1],[40,1]]};
  socket.onmessage({data:JSON.stringify(randomized)});

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
  assert.equal(elements.placement.hidden,true);

  socket.onmessage({data:JSON.stringify(emptyState('playing',true))});
  socket.onmessage({data:JSON.stringify({type:'f',h:[0,0,0,0,0]})});
  assert.equal(elements.profile.hidden,true);
  assert.equal(elements.placement.hidden,true);
  assert.equal(elements.hud.hidden,false);
  assert.equal(elements.app.className,'in-battle');
  assert.match(elements.youPanel.className,/active/);
  assert.doesNotMatch(elements.enemyPanel.className,/active/);
  assert.equal(elements.hudTurn.textContent,'YOUR TURN · SELECT TARGET');
  assert.equal(elements.ownSummary.children.length,5);
  assert.equal(elements.enemySummary.children.length,5);

  elements.target.children[12].onclick();
  assert.equal(sent.length,4);
  assert.match(elements.target.children[12].className,/selected-target/);
  elements.target.children[12].onclick();
  assert.deepEqual(sent[4],['fire',token,4,12]);
  const fireReply={...ready,revision:5};
  socket.onmessage({data:JSON.stringify(fireReply)});
  socket.onmessage({data:JSON.stringify(emptyState('playing',false,-1,0,'AAAAAw'+ 'A'.repeat(28)))});
  socket.onmessage({data:JSON.stringify({type:'f',h:[1,0,0,0,0]})});
  assert.doesNotMatch(elements.youPanel.className,/active/);
  assert.match(elements.enemyPanel.className,/active/);
  assert.equal(elements.hudTurn.textContent,'SPIKY WINK BEARD TURN');

  socket.onmessage({data:JSON.stringify(emptyState('playing',false,-1,1,'AAAAAw'+ 'A'.repeat(28)))});
  socket.onmessage({data:JSON.stringify({type:'f',h:[5,0,0,0,0]})});
  assert.equal(elements.eventBanner.textContent,'YOU SANK SPIKY WINK BEARD: CARRIER');
  assert.match(elements.eventBanner.className,/show/);
  assert.match(elements.enemySummary.children[0].className,/sunk/);
  assert.ok(elements.enemySummary.children[0].children.every(segment=>/sunk-segment/.test(segment.className) && !/ hit/.test(segment.className)));

  socket.close();reconnect();await flush();
  assert.equal(sockets.length,2);
  const resumed=sockets[1];resumed.onopen();
  resumed.onmessage({data:JSON.stringify({type:'peer',name:'SPIKY WINK BEARD'})});
  assert.deepEqual(sent[5],['resume',token,5,resume]);
  resumed.onmessage({data:JSON.stringify(fireReply)});
  resumed.onmessage({data:JSON.stringify({type:'resume',token:resume})});
  resumed.onmessage({data:JSON.stringify(emptyState('playing',true))});
  resumed.onmessage({data:JSON.stringify({type:'f',h:[1,0,0,0,0]})});

  elements.surrender.onclick();
  assert.equal(sent.length,6);
  assert.equal(elements.surrender.textContent,'CONFIRM SURRENDER');
  elements.surrender.onclick();
  assert.deepEqual(sent[6],['surrender',token,5]);
  resumed.onmessage({data:JSON.stringify({type:'error'})});

  resumed.onmessage({data:JSON.stringify(emptyState('finished',false,1,3))});
  assert.equal(elements.resultBanner.textContent,'VICTORY');
  assert.match(elements.resultBanner.className,/show/);
  assert.equal(elements.surrender.hidden,true);
  assert.equal(elements.rematch.hidden,false);
  elements.rematch.onclick();
  assert.deepEqual(sent[7],['rematch',token,5]);
  resumed.onmessage({data:JSON.stringify(emptyState('placement'))});
  const replay={...view,revision:6,slots:ready.slots};
  resumed.onmessage({data:JSON.stringify(replay)});
  assert.equal(store.size,0);
  assert.equal(elements.placement.hidden,false);

  events.pagehide();assert.equal(reconnect,null);
  events.pageshow({persisted:true});await flush();
  assert.equal(sockets.length,3);
  console.log('Browser page: player profile controls, peer identity, sunk announcements/visuals, active-turn inversion, fleet damage boxes, two-tap fire, surrender, secure resume and rematch passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
