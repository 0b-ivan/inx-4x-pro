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
    textContent: "", className: "", type: "", style: {},
    append(...nodes) { this.children.push(...nodes); },
    replaceChildren(...nodes) { this.children = nodes; },
    addEventListener(name, callback) { events[name] = callback; },
    setAttribute(name, value) { this.attrs[name] = value; },
    dispatch(name, args={}) { assert.ok(events[name], "handler "+name); events[name](args); }
  };
}
const ids = [
  "connection","round","timer","pauseButton","join","joinForm","joinButton","joinHint",
  "nick","lobby","roster","play","playMeta","promptText","answerPreview","choices","count",
  "footCount","submit","waiting","waitingCount","judging","judgeHeader",
  "judgePrompt","candidateList","judgeSelection","confirmJudge",
  "result","winner","winnerText","scores","nextHint",
  "final","finalEyebrow","podium","standings","votes","againButton",
  "hold","holdEyebrow","holdTitle","holdText","resumeButton","removed","rejoinButton",
  "fame","fameButton","famePlayers","fameQuotes","famePlayersHeading","fameQuotesHeading","famePrev","fameNext","fameClose","strikeButton"
];
const nodes = Object.fromEntries(ids.map(x => [x, element()]));
const store = new Map();
const sessionStorage = {
  getItem: x => store.get(x) || null,
  setItem: (x,v) => store.set(x,v),
  removeItem: x => store.delete(x)
};
const localItems=new Map();
const localStorage={
  getItem:key=>localItems.get(key)||null,
  setItem:(key,value)=>localItems.set(key,value),
  removeItem:key=>localItems.delete(key)
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
assert.match(html,/height:100dvh/);  // play screen fits the viewport; no page scroll hides cards
assert.doesNotMatch(html,/cdn\.|fonts\.googleapis/);
const match = html.match(/<script>([\s\S]*?)<\/script>/);
assert.ok(match, "embedded script");
new vm.Script(match[1]); // syntax check
vm.runInNewContext(match[1], {
  document: {getElementById:id => {
      assert.ok(nodes[id], "unknown id: "+id); return nodes[id];
    }, createElement: () => element(), createTextNode: text => ({textContent: text})},
  sessionStorage, localStorage, location:{hostname:"crossplay.local"}, navigator:{language:"de-DE"},
  WebSocket:Socket, setTimeout: fn => {timers.push(fn);return timers.length;},
  setInterval: () => 0
});
assert.equal(sockets.length,1);
const first=sockets[0];
assert.equal(nodes.connection.textContent,"VERBINDE");
assert.equal(nodes.join.hidden,false);
first.open();
assert.equal(nodes.connection.textContent,"LOKAL / VERBUNDEN");
first.message({type:"hello",lang:"en"});
assert.equal(nodes.connection.textContent,"LOCAL / CONNECTED");
first.message({type:"error",code:"session_full"});
assert.equal(nodes.joinHint.textContent,"ERROR: SESSION FULL");
for(const [code,label] of [["rejected","REQUEST REJECTED"],["already_joined","ALREADY JOINED"],
                           ["game_running","GAME ALREADY RUNNING"]]){
  first.message({type:"error",code});
  assert.equal(nodes.joinHint.textContent,"ERROR: "+label);
}
first.message({type:"error",code:"unrecognized_error"});
assert.equal(nodes.joinHint.textContent,"ERROR: unrecognized_error");
first.message({type:"hello",lang:"de"});
assert.equal(nodes.connection.textContent,"LOKAL / VERBUNDEN");
assert.equal(nodes.joinHint.textContent,"FEHLER: unrecognized_error");
first.message({type:"error",code:"session_full"});
assert.equal(nodes.joinHint.textContent,"FEHLER: SITZUNG VOLL");
for(const [code,label] of [["rejected","ANFRAGE ABGELEHNT"],["already_joined","BEREITS BEIGETRETEN"],
                           ["game_running","SPIEL LÄUFT BEREITS"]]){
  first.message({type:"error",code});
  assert.equal(nodes.joinHint.textContent,"FEHLER: "+label);
}

nodes.nick.value="Ivan";
nodes.joinForm.dispatch("submit",{preventDefault(){}});
assert.deepEqual(first.sent.at(-1),{type:"join",name:"Ivan"});
const token1="0123456789abcdef0123456789abcdef";
const token2="fedcba9876543210fedcba9876543210";
first.message({type:"welcome",pid:"11112222deadbeef",word:"Toast",token:token1,revision:0,game:{
  phase:"Lobby",round:0,players:[
    {name:"Ivan",seated:true,connected:true,points:0},
    {name:"Max",seated:true,connected:true,points:0}]
  },private:{}});
assert.equal(store.get("caw.session.token"),token1);
assert.equal(nodes.lobby.hidden,false);
assert.equal(localItems.get("caw.player.id"),"11112222deadbeef");
nodes.fameButton.dispatch("click");
assert.deepEqual(first.sent.at(-1),{type:"fame",token:token1,page:0});
first.message({type:"fame",page:0,pages:2,section:"players",total:1,players:[
  {name:"Ivan",word:"Toast",games:2,wins:1,rounds:4}],quotes:[]});
assert.equal(nodes.fame.hidden,false);
assert.equal(nodes.famePlayers.children.length,1);
assert.equal(nodes.fameQuotes.hidden,true);
nodes.fameNext.dispatch("click");
assert.deepEqual(first.sent.at(-1),{type:"fame",token:token1,page:1});
first.message({type:"fame",page:1,pages:2,section:"quotes",total:1,
  players:[],quotes:[{name:"Ivan",sentence:"Hallo ____."}]});
assert.equal(nodes.famePlayers.hidden,true);
assert.equal(nodes.fameQuotes.children.length,1);
assert.equal(nodes.fameNext.disabled,true);
nodes.fameClose.dispatch("click");
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
assert.equal(nodes.count.textContent,"1/2 GEWÄHLT");
assert.equal(nodes.choices.children[1].attrs["aria-pressed"],"true");
nodes.choices.children[3].dispatch("click");
assert.equal(nodes.choices.scrollTop,96);
assert.equal(nodes.choices.children[3].children.at(-1).textContent," <2/2> ");
assert.equal(nodes.submit.disabled,false);
assert.equal(nodes.footCount.textContent,"2/2 GEWÄHLT");
assert.equal(nodes.strikeButton.disabled,false);
nodes.strikeButton.dispatch("click");
assert.deepEqual(first.sent.at(-1),{type:"strike",token:token1,slot:3});

// The picked answers sit in the prompt's blanks, in play order, emphasized.
const preview=nodes.answerPreview.children;
assert.ok(preview.filter(part=>part.style&&part.style.textDecoration==="underline").length===2);
nodes.submit.dispatch("click");
assert.deepEqual(first.sent.at(-1),{
  type:"submit",token:token1,revision:0,slots:[1,3]});
assert.equal(nodes.submit.disabled,true);
first.message({type:"state",revision:1,game:{...playing,submitted:1},
  private:{isCzar:false,submitted:true,hand:cards}});
assert.equal(nodes.waiting.hidden,false);
assert.equal(nodes.waitingCount.textContent,"1/2 ERHALTEN");
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
  winningCards:["Kaffeemaschine mit IPv6","Vier vergessene Backups"],
  players:[{name:"Ivan",seated:true,points:1},{name:"Max",seated:true,points:0}]},
  private:{isCzar:true}});
assert.equal(nodes.result.hidden,false);
assert.equal(nodes.winner.textContent,"Ivan");
assert.equal(nodes.winnerText.children.filter(x=>x.style&&x.style.textDecoration==="underline").length,2);
assert.equal(nodes.candidateList.children[0].children[1].children.filter(x=>x.style&&x.style.textDecoration==="underline").length,2);
assert.equal(nodes.scores.children.length,2);
first.close();
assert.equal(nodes.connection.textContent,"OFFLINE");
assert.equal(timers.length,1);
timers.shift()();
assert.equal(sockets.length,2);
const second=sockets[1];second.open();
assert.deepEqual(second.sent[0],{type:"resume",token:token1});
second.message({type:"welcome",token:token2,revision:2,game:playing,
  private:{isCzar:false,submitted:false,hand:cards}});
assert.equal(store.get("caw.session.token"),token2);
// Clock: the phone counts down from the X4's "left" by itself.
second.message({type:"state",revision:2,game:{...playing,rounds:10,clock:"answers",left:42000},
  private:{isCzar:false,submitted:false,hand:cards}});
assert.equal(nodes.round.textContent,"RUNDE "+playing.round+"/10");
assert.equal(nodes.timer.textContent,"ZEIT 0:42");
assert.equal(nodes.pauseButton.hidden,false);
// Anyone can pause; the pause carries the ticket only.
nodes.pauseButton.dispatch("click");
assert.deepEqual(second.sent.at(-1),{type:"pause",token:token2});
second.message({type:"state",revision:2,game:{...playing,clock:"answers",left:30000,pausedBy:"Max"},
  private:{isCzar:false,submitted:false,hand:cards,pausedByMe:false}});
assert.equal(nodes.hold.hidden,false);
assert.equal(nodes.holdTitle.textContent,"PAUSIERT VON Max");
assert.equal(nodes.resumeButton.hidden,true);
assert.equal(nodes.pauseButton.hidden,true);
second.message({type:"state",revision:2,game:{...playing,pausedBy:"Ivan"},
  private:{isCzar:false,submitted:false,hand:cards,pausedByMe:true}});
assert.equal(nodes.resumeButton.hidden,false);
nodes.resumeButton.dispatch("click");
assert.deepEqual(second.sent.at(-1),{type:"unpause",token:token2});
// A needed player gone for 15 s: everyone waits and sees who.
second.message({type:"state",revision:2,game:{...playing,blockedOn:"Max"},
  private:{isCzar:false,submitted:false,hand:cards}});
assert.equal(nodes.hold.hidden,false);
assert.equal(nodes.holdTitle.textContent,"WARTE AUF Max");
second.message({type:"state",revision:2,game:{...playing,pausedByHost:true},
  private:{isCzar:false,submitted:false,hand:cards}});
assert.equal(nodes.holdTitle.textContent,"PAUSIERT VON DEM X4");
// Auto mode countdown in the reveal.
second.message({type:"state",revision:2,game:{...playing,phase:"Reveal",clock:"next",left:8000,
  winnerName:"Ivan",winningText:"x",players:[]},private:{isCzar:false}});
assert.equal(nodes.nextHint.textContent,"NÄCHSTE RUNDE IN 0:08");
// Ceremony: a crown for every leader, again votes, host end.
second.message({type:"state",revision:2,game:{...playing,phase:"Finished",endedByHost:true,votes:1,
  players:[{name:"Ivan",seated:true,points:3},{name:"Max",seated:true,points:3},
           {name:"Zoe",seated:true,points:1},{name:"",seated:false,points:9}]},
  private:{isCzar:false,voted:false}});
assert.equal(nodes.final.hidden,false);
assert.equal(nodes.finalEyebrow.textContent,"/// VOM X4 BEENDET");
assert.equal(nodes.podium.children.length,2);
assert.equal(nodes.podium.children[0].children[0].className,"crown");
assert.match(nodes.podium.children[1].children[1].textContent,/^Max/);
assert.equal(nodes.standings.children.length,3);
assert.match(nodes.standings.children[2].textContent,/^Zoe/);
assert.equal(nodes.votes.textContent,"1 WOLLEN NOCHMAL");
nodes.againButton.dispatch("click");
assert.deepEqual(second.sent.at(-1),{type:"again",token:token2});
assert.equal(nodes.againButton.disabled,true);
// Removed by the host: ticket gone, own screen, then back to join.
second.message({type:"kicked"});
assert.equal(store.get("caw.session.token"),undefined);
assert.equal(nodes.removed.hidden,false);
nodes.rejoinButton.dispatch("click");
assert.equal(nodes.join.hidden,false);
second.message({type:"welcome",token:token2,revision:0,game:playing,
  private:{isCzar:false,submitted:false,hand:cards}});
second.message({type:"expired"});
assert.equal(store.get("caw.session.token"),undefined);
assert.equal(nodes.join.hidden,false);
// The X4 releases a socket that never (re)joins; a ticketless page stays
// idle instead of reconnecting every 1.5 s and holding a server slot.
second.close();
assert.equal(timers.length,0);
assert.equal(nodes.connection.textContent,"BEREIT / BEITRETEN");
// 24 UTF-8 bytes (three umlauts) exceed the X4's 23-byte seat name.
nodes.nick.value="J\u00fcrgen-Sch\u00e4fer-M\u00fcller";
nodes.joinForm.dispatch("submit",{preventDefault(){}});
assert.match(nodes.joinHint.textContent,/ZU LANG/);
assert.equal(sockets.length,2);
// JOIN reconnects on demand and sends the join once the socket opens.
nodes.nick.value="J\u00fcrgen";
nodes.joinForm.dispatch("submit",{preventDefault(){}});
assert.equal(sockets.length,3);
assert.equal(nodes.joinHint.textContent,"VERBINDE MIT X4");
const third=sockets[2];third.open();
assert.deepEqual(third.sent[0],{type:"join",name:"J\u00fcrgen",pid:"11112222deadbeef"});
// A specific rejection reason from the X4 is shown verbatim.
third.message({type:"error",message:"SESSION FULL"});
assert.equal(nodes.joinHint.textContent,"FEHLER: SESSION FULL");
// A failed on-demand connection reports the error and does not loop.
third.close();
nodes.joinForm.dispatch("submit",{preventDefault(){}});
const fourth=sockets[3];fourth.close();
assert.equal(nodes.joinHint.textContent,"FEHLER: KEINE VERBINDUNG ZUM X4");
assert.equal(timers.length,0);
console.log("cardparty browser: DOM/socket flow, auth token rotation, Pick 2, judgment, reveal, idle release, UTF-8 name limit OK");
