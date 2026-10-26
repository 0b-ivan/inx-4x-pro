const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync('../../src/apps_local/connectfour/web/ConnectFourBrowserPage.html','utf8');
const ids=['status','detail','join','rematch','board','columns'];
function element() {
  return {textContent:'',hidden:false,disabled:false,children:[],className:'',attrs:{},
    appendChild(child){this.children.push(child);},
    replaceChildren(...children){this.children=children;},
    setAttribute(name,value){this.attrs[name]=value;}};
}
const elements=Object.fromEntries(ids.map(id=>[id,element()]));
const sockets=[],sent=[],store=new Map(),listeners={};
let fetchCount=0,retry=null;
class Socket {
  static OPEN=1;
  constructor(url){assert.equal(url,'ws://crossplay.local:81/connectfour');this.readyState=1;sockets.push(this);}
  send(data){sent.push(JSON.parse(data));}
  close(){this.readyState=3;if(this.onclose)this.onclose();}
}
const first='0123456789abcdef0123456789abcdef';
const second='123456789abcdef0123456789abcdef0';
const secret='fedcba9876543210fedcba9876543210';
const scope={
  document:{getElementById:id=>elements[id],createElement:element},
  sessionStorage:{getItem:key=>store.get(key)||null,setItem:(key,value)=>store.set(key,value),removeItem:key=>store.delete(key)},
  window:{addEventListener:(name,cb)=>listeners[name]=cb},
  WebSocket:Socket,location:{hostname:'crossplay.local'},
  fetch:async(path,options)=>{assert.equal(path,'/connectfour/session');assert.equal(options.cache,'no-store');++fetchCount;return {ok:true,text:async()=>fetchCount===1?first:second};},
  setTimeout:(callback,ms)=>{assert.equal(ms,1800);retry=callback;return 42;},
  clearTimeout:()=>{retry=null;},
};
vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],scope);
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const state=(phase,r,turn,board='0'.repeat(42),outcome=0,last=[-1,-1])=>({type:'c4',phase,r,turn,board,outcome,last});
(async()=>{
  await flush();
  assert.equal(elements.columns.children.length,7);
  assert.equal(elements.board.children.length,42);
  const ws=sockets[0];ws.onopen();
  assert.equal(elements.join.hidden,true); // No claim before authoritative first snapshot.
  ws.onmessage({data:JSON.stringify(state('waiting',0,1))});
  assert.equal(elements.join.hidden,false);
  assert.equal(elements.join.disabled,false);
  elements.join.onclick();assert.deepEqual(sent[0],['join',first,0]);
  ws.onmessage({data:JSON.stringify({type:'resume',token:secret})});
  ws.onmessage({data:JSON.stringify(state('playing',0,1))});
  assert.equal(elements.join.hidden,true);
  assert.equal(elements.columns.children[1].disabled,true);
  assert.equal(store.size,1);
  const x4='1'+'0'.repeat(41);
  ws.onmessage({data:JSON.stringify(state('playing',1,2,x4,0,[0,0]))});
  assert.equal(elements.columns.children[1].disabled,false);
  elements.columns.children[1].onclick();
  assert.deepEqual(sent[1],['drop',first,1,1]);
  const both='1'+'0'.repeat(5)+'2'+'0'.repeat(35);
  ws.onmessage({data:JSON.stringify(state('playing',2,1,both,0,[1,0]))});
  assert.equal(elements.columns.children[1].disabled,true);
  ws.close();assert.ok(retry);
  retry();await flush();assert.equal(sockets.length,2);
  const restored=sockets[1];restored.onopen();
  assert.deepEqual(sent[2],['resume',second,2,secret]);
  restored.onmessage({data:JSON.stringify({type:'resume',token:secret})});
  restored.onmessage({data:JSON.stringify(state('finished',7,1,both,1,[0,3]))});
  assert.match(elements.status.textContent,/X4 PRO WINS/);
  assert.equal(elements.rematch.hidden,false);
  elements.rematch.onclick();assert.deepEqual(sent[3],['rematch',second,7]);
  restored.onmessage({data:JSON.stringify(state('playing',8,1))});
  assert.equal(elements.rematch.hidden,true);
  assert.equal(elements.board.children.length,42);
  listeners.pagehide();assert.equal(retry,null);
  console.log('Connect Four browser page: board, accessible column moves, X4 turn, reconnect/resume and rematch passed');
})().catch(e=>{console.error(e);process.exitCode=1;});