const { chromium } = require('playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
(async () => {
  const browser = await chromium.launch({headless:true, args:['--no-sandbox']});
  try {
    const page = await browser.newPage({viewport:{width:390,height:844}});
    const errors=[]; page.on('pageerror', e=>errors.push(e.message));
    const posts=[];
    const feed={index:0,name:'Nextcloud <news>',url:'https://cloud.example/feed',username:'ivan',hasPassword:true};
    await page.route('http://reader.test/**', async route=>{
      const req=route.request(), url=new URL(req.url());
      if (url.pathname === '/settings') return route.fulfill({contentType:'text/html',body:fs.readFileSync(path.join(__dirname,'../../src/network/html/SettingsPage.html'),'utf8')});
      if (req.method()==='POST') {
        posts.push({url:url.pathname,data:req.postDataJSON()});
        return route.fulfill({status:url.pathname.endsWith('/test')?422:200,body:url.pathname.endsWith('/test')?'HTTP 401: login rejected':'OK'});
      }
      return route.fulfill({contentType:'application/json',body:JSON.stringify(url.pathname==='/api/rss'?[feed]:[])});
    });
    await page.goto('http://reader.test/settings');
    await page.locator('#rss-name-0').waitFor();
    assert.equal(await page.locator('#rss-name-0').inputValue(),feed.name);
    assert.equal(await page.locator('#rss-pass-0').inputValue(),'');
    await page.locator('#rss-0 .btn-save-server').click();
    await page.waitForFunction(()=>document.getElementById('message').textContent==='RSS feed saved!');
    assert.equal(Object.hasOwn(posts.at(-1).data,'password'),false);
    await page.locator('#rss-pass-0').fill(' app-secret ');
    await page.locator('#rss-0 .btn-save-server').click();
    await page.waitForFunction(()=>document.getElementById('rss-pass-0').value==='');
    assert.equal(posts.at(-1).data.password,' app-secret ');
    await page.locator('#rss-clear-0').check();
    await page.locator('#rss-0 .btn-save-server').click();
    await page.waitForFunction(()=>!document.getElementById('rss-clear-0').checked);
    assert.equal(posts.at(-1).data.password,'');
    await page.getByRole('button',{name:'+ Add feed',exact:true}).click();
    await page.locator('#rss-url-new').fill(' https://cloud.example/feed ');
    await page.locator('#rss-user-new').fill(' ivan ');
    await page.locator('#rss-pass-new').fill('secret');
    await page.locator('#rss-new').getByRole('button',{name:'Test connection'}).click();
    await page.waitForFunction(()=>document.getElementById('message').textContent.includes('HTTP 401'));
    assert.equal(posts.at(-1).url,'/api/rss/test');
    assert.equal(posts.at(-1).data.url,'https://cloud.example/feed');
    assert.equal(posts.at(-1).data.username,'ivan');
    assert.equal(Object.hasOwn(posts.at(-1).data,'index'),false);
    assert.equal(await page.locator('#rss-pass-new').inputValue(),'secret');
    assert.equal(await page.locator('#rss-new').getByRole('button',{name:'Test connection'}).isEnabled(),true);
    await page.locator('#rss-container').screenshot({path:'/tmp/rss-web-mobile.png'});
    page.on('dialog',d=>d.accept());
    await page.locator('#rss-0 .btn-delete').click();
    await page.waitForFunction(()=>document.getElementById('message').textContent==='RSS feed deleted');
    assert.deepEqual(posts.at(-1),{url:'/api/rss/delete',data:{index:0}});
    assert.deepEqual(errors,[]);
    console.log('PASS: RSS mobile form, escaping, password preserve/replace/clear, unsaved test, error display, delete');
  } finally { await browser.close(); }
})().catch(e=>{console.error(e);process.exit(1);});
