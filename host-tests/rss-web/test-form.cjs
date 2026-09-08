const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync(path.join(__dirname, '../../src/network/html/SettingsPage.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script); // Check the complete page's JavaScript syntax.
const elements = {};
const posts = [];
const messages = [];
let responseOk = true;
const context = vm.createContext({
  document: {getElementById:id=>elements[id]},
  fetch: async (url, options)=>{
    posts.push({url,data:JSON.parse(options.body)});
    return {ok:responseOk,text:async()=>responseOk?'Feed OK':'HTTP 401: login rejected'};
  },
  showMessage:(text,error)=>messages.push({text,error}),
  escapeHtml:s=>s.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;'),
  confirm:()=>true
});
const start=script.indexOf('  // --- RSS Feed Management ---');
const end=script.indexOf('  // Sequential, not concurrent:',start);
vm.runInContext(script.slice(start,end),context);
vm.runInContext('loadRssFeeds = async () => {};',context);
function form(id) {
  for(const key of ['name','url','user','pass']) elements[`rss-${key}-${id}`]={value:''};
  elements[`rss-clear-${id}`]={checked:false};
  elements[`rss-name-${id}`].value=' Nextcloud ';
  elements[`rss-url-${id}`].value=' https://cloud.example/feed ';
  elements[`rss-user-${id}`].value=' ivan ';
}
(async()=>{
  form(0);
  await context.saveRssFeed(0);
  assert.equal(Object.hasOwn(posts.at(-1).data,'password'),false);
  assert.equal(posts.at(-1).data.url,'https://cloud.example/feed');
  elements['rss-pass-0'].value=' secret ';
  await context.saveRssFeed(0);
  assert.equal(posts.at(-1).data.password,' secret ');
  elements['rss-clear-0'].checked=true;
  await context.saveRssFeed(0);
  assert.equal(posts.at(-1).data.password,'');
  form('new'); responseOk=false;
  const button={disabled:false};
  await context.saveRssFeed(-1,true,button);
  assert.equal(posts.at(-1).url,'/api/rss/test');
  assert.equal(Object.hasOwn(posts.at(-1).data,'index'),false);
  assert.equal(button.disabled,false);
  assert.equal(messages.at(-1).error,true);
  assert.match(messages.at(-1).text,/HTTP 401/);
  const rendered=context.renderRssFeed({name:'<script>',hasPassword:true,password:'DO-NOT-EXPOSE'},0);
  assert.match(rendered,/&lt;script>/);
  assert.equal(rendered.includes('DO-NOT-EXPOSE'),false);
  responseOk=true;
  await context.deleteRssFeed(0);
  assert.equal(posts.at(-1).url,'/api/rss/delete');
  assert.equal(posts.at(-1).data.index,0);
  console.log('PASS: full JS syntax; preserve, replace and clear password; trim URL; unsaved test; error display; escaped rendering; delete');
})().catch(e=>{console.error(e);process.exit(1)});
