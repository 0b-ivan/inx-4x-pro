// Node-only browser client smoke: document mock and fake WebSocket.
// No ESP32, network, Playwright dependency or external packages.
const fs = require("node:fs");
const vm = require("node:vm");
const assert = require("node:assert/strict");
const path = require("node:path");
const html = fs.readFileSync(path.join(__dirname,
  "../../src/apps_local/cardparty/web/CardPartyPage.html"), "utf8");

function element() {
  const events = Object.create(null);
  return {
    hidden: false, disabled: false, value: "", children: [], attrs: {},
    textContent: "", className: "", type: "",
    append(...nodes) { this.children.push(...nodes); },
    replaceChildren(...nodes) { this.children = nodes; },
    addEventListener(name, callback) { events[name] = callback; },
    setAttribute(name, value) { this.attrs[name] = value; },
    dispatch(name, args={}) { assert.ok(events[name], "handler "+name); events[name](args); }
  };
}
const ids = [
  "connection","session","round","join","joinForm","joinButton","joinHint",
  "nick","lobby","roster","play","playMeta","promptText","choices","count",
  "footCount","submit","waiting","waitingCount","judging","judgeHeader",
  "judgePrompt","candidateList","judgeSelection","confirmJudge",
  "result","winner","winnerText","scores"
];
const nodes = Object.fromEntries(ids.map(x => [x, element()]));
const store = new Map();
const sessionStorage = {
  getItem: x => store.get(x) || null,
  setItem: (x,v) => store.set(x,v),
  removeItem: x => store.delete(x)
};
const timers = [];
const sockets = [];
class Socket {
  static OPEN = 1;
  constructor(url) {
    assert.equal(url, "ws://crossplay.local:81/cardparty");
    this.readyState = 0;
    this.handlers = {};
    this.sent = [];
    sockets.push(this);
  }
  addEventListener(name, fn) { this.handlers[name] = fn; }
  emit(name, payload={}) { assert.ok(this.handlers[name]); this.handlers[name](payload); }
  send(payload) { this.sent.push(JSON.parse(payload)); }
  message(data) { this.emit("message",{data:JSON.stringify(data)}); }
  open() { this.readyState=Socket.OPEN; this.emit("open"); }
  close() { this.readyState=3; this.emit("close"); }
}
assert.match(html,/background:#000/);
assert.match(html,/aria-pressed/);
assert.match(html,/min-height:48px/);
assert.match(html,/position:sticky/);
assert.doesNotMatch(html,/cdn\.|fonts\.googleapis/);
const match = html.match(/<script>([\s\S]*?)<\/script>/);
assert.ok(match, "embedded script");
new vm.Script(match[1]); // syntax check
vm.runInNewContext(match[1], {
  document: {getElementById:id => {
      assert.ok(nodes[id], "unknown id: "+id); return nodes[id];
    }, createElement: () => element()},
  sessionStorage, location:{hostname:"crossplay.local"},
  WebSocket:Socket, setTimeout: fn => {timers.push(fn);return timers.length;}
});
assert.equal(sockets.length,1);
const first=sockets[0];
assert.equal(nodes.connection.textContent,"CONNECTING");
assert.equal(nodes.join.hidden,false);
first.open();
assert.equal(nodes.connection.textContent,"LOCAL / CONNECTED");
nodes.nick.value="Ivan";
nodes.joinForm.dispatch("submit",{preventDefault(){}});
assert.deepEqual(first.sent.at(-1),{type:"join",name:"Ivan"});
const token1="0123456789abcdef0123456789abcdef";
const token2="fedcba9876543210fedcba9876543210";
first.message({type:"welcome",token:token1,revision:0,game:{
  phase:"Lobby",round:0,players:[
    {name:"Ivan",seated:true,connected:true,points:0},
    {name:"Max",seated:true,connected:true,points:0}]
  },private:{}});
assert.equal(store.get("caw.session.token"),token1);
assert.equal(nodes.lobby.hidden,false);
assert.equal(nodes.roster.children.length,2);
assert.match(nodes.roster.children[0].textContent,/Ivan/);
const cards = [
  {text:"Ein Praktikant mit Root-Zugriff"},
  {text:"Kaffeemaschine mit IPv6"},
  {text:"Ein einzelner DNS-Server"},
  {text:"Vier vergessene Backups"}
];
const playing={phase:"Playing",round:2,pick:2,promptText:"Warum ist der Server down? ____ und ____",
  czarName:"Max",submitted:0,expected:2};
first.message({type:"state",revision:0,game:playing,private:{isCzar:false,submitted:false,hand:cards}});
assert.equal(nodes.play.hidden,false);
assert.equal(nodes.choices.children.length,4);
assert.equal(nodes.submit.disabled,true);
assert.equal(nodes.promptText.textContent,playing.promptText);
nodes.choices.scrollTop=96; // selecting a lower card must not jump to the top
nodes.choices.children[1].dispatch("click");
assert.equal(nodes.choices.scrollTop,96);
assert.equal(nodes.choices.children[1].children.at(-1).textContent," <1/2> ");
assert.equal(nodes.submit.disabled,true);
assert.equal(nodes.count.textContent,"1/2 SELECTED");
assert.equal(nodes.choices.children[1].attrs["aria-pressed"],"true");
nodes.choices.children[3].dispatch("click");
assert.equal(nodes.choices.scrollTop,96);
assert.equal(nodes.choices.children[3].children.at(-1).textContent," <2/2> ");
assert.equal(nodes.submit.disabled,false);
assert.equal(nodes.footCount.textContent,"2/2 SELECTED");
nodes.submit.dispatch("click");
assert.deepEqual(first.sent.at(-1),{
  type:"submit",token:token1,revision:0,slots:[1,3]});
assert.equal(nodes.submit.disabled,true);
first.message({type:"state",revision:1,game:{...playing,submitted:1},
  private:{isCzar:false,submitted:true,hand:cards}});
assert.equal(nodes.waiting.hidden,false);
assert.equal(nodes.waitingCount.textContent,"1/2 RECEIVED");
first.message({type:"state",revision:1,
  game:{...playing,phase:"Judging",submissions:[
    {cards:["SRE mit Root","Zusätzliche Downtime"]},
    {cards:["Datenbank über Nacht","Der eine DNS-Server"]}]},
  private:{isCzar:false,submitted:true,hand:cards}});
assert.equal(nodes.judging.hidden,false);
assert.equal(nodes.confirmJudge.disabled,true); // nonjudge must not vote
// Same client is judge in following mock state (server remains authoritative).
first.message({type:"state",revision:1,
  game:{...playing,phase:"Judging",submissions:[
    {cards:["SRE mit Root","Zusätzliche Downtime"]},
    {cards:["Datenbank über Nacht","Der eine DNS-Server"]}]},
  private:{isCzar:true,submitted:false,hand:cards}});
nodes.candidateList.scrollTop=42;
nodes.candidateList.children[0].dispatch("click");
assert.equal(nodes.candidateList.scrollTop,42);
assert.equal(nodes.confirmJudge.disabled,false);
nodes.confirmJudge.dispatch("click");
assert.deepEqual(first.sent.at(-1),{
  type:"judge",token:token1,revision:1,selected:0});
first.message({type:"state",revision:2,game:{...playing,phase:"Reveal",
  winnerName:"Ivan",winningText:"Kaffeemaschine mit IPv6",
  players:[{name:"Ivan",seated:true,points:1},{name:"Max",seated:true,points:0}]},
  private:{isCzar:true}});
assert.equal(nodes.result.hidden,false);
assert.equal(nodes.winner.textContent,"Ivan");
assert.equal(nodes.scores.children.length,2);
first.close();
assert.equal(nodes.connection.textContent,"DISCONNECTED");
assert.equal(timers.length,1);
timers.shift()();
assert.equal(sockets.length,2);
const second=sockets[1];second.open();
assert.deepEqual(second.sent[0],{type:"resume",token:token1});
second.message({type:"welcome",token:token2,revision:2,game:playing,
  private:{isCzar:false,submitted:false,hand:cards}});
assert.equal(store.get("caw.session.token"),token2);
second.message({type:"expired"});
assert.equal(store.get("caw.session.token"),undefined);
assert.equal(nodes.join.hidden,false);
// The X4 releases a socket that never (re)joins; a ticketless page stays
// idle instead of reconnecting every 1.5 s and holding a server slot.
second.close();
assert.equal(timers.length,0);
assert.equal(nodes.connection.textContent,"IDLE / JOIN TO CONNECT");
// 24 UTF-8 bytes (three umlauts) exceed the X4's 23-byte seat name.
nodes.nick.value="J\u00fcrgen-Sch\u00e4fer-M\u00fcller";
nodes.joinForm.dispatch("submit",{preventDefault(){}});
assert.match(nodes.joinHint.textContent,/NAME TOO LONG/);
assert.equal(sockets.length,2);
// JOIN reconnects on demand and sends the join once the socket opens.
nodes.nick.value="J\u00fcrgen";
nodes.joinForm.dispatch("submit",{preventDefault(){}});
assert.equal(sockets.length,3);
assert.equal(nodes.joinHint.textContent,"CONNECTING TO X4");
const third=sockets[2];third.open();
assert.deepEqual(third.sent[0],{type:"join",name:"J\u00fcrgen"});
// A specific rejection reason from the X4 is shown verbatim.
third.message({type:"error",message:"SESSION FULL"});
assert.equal(nodes.joinHint.textContent,"ERROR: SESSION FULL");
// A failed on-demand connection reports the error and does not loop.
third.close();
nodes.joinForm.dispatch("submit",{preventDefault(){}});
const fourth=sockets[3];fourth.close();
assert.equal(nodes.joinHint.textContent,"ERROR: NO CONNECTION TO X4");
assert.equal(timers.length,0);
console.log("cardparty browser: DOM/socket flow, auth token rotation, Pick 2, judgment, reveal, idle release, UTF-8 name limit OK");
