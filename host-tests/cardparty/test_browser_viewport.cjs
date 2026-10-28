// Real-browser layout check of the CardParty phone page (Chromium via
// Playwright): 320 px, 390 px and desktop, long German text at the server's
// limits, Pick 3 ordering, judging with eight answers. The WebSocket is a
// page-side fake, so no X4 and no network are involved.
//
// Screenshots go to .pio/cardparty-browser-viewports/ for visual review.
// Skips (exit 0) when Playwright is not installed, unless
// CARDPARTY_REQUIRE_BROWSER is set.
const fs = require("node:fs");
const path = require("node:path");
const assert = require("node:assert/strict");

let chromium;
try { ({ chromium } = require("playwright")); } catch {
  // CI installs Playwright and sets CARDPARTY_REQUIRE_BROWSER so this never skips there.
  if (process.env.CARDPARTY_REQUIRE_BROWSER) throw new Error("playwright required but not installed");
  console.log("SKIP cardparty viewport: playwright not installed");
  process.exit(0);
}

const root = path.join(__dirname, "../..");
const html = fs.readFileSync(path.join(root, "src/apps_local/cardparty/web/CardPartyPage.html"), "utf8");
const outDir = path.join(root, ".pio/cardparty-browser-viewports");
fs.mkdirSync(outDir, { recursive: true });

// Longest content the X4 can send: names up to 23 UTF-8 bytes, long German
// prompt and answers with umlauts and long compounds.
const names = ["Jürgen-Schäfer-Müller", "Zoë", "Ännchen von Thäräu", "Maximilian-Alexander",
               "Öztürk-Weißenböck", "Lüüüüüüüüüüü", "BERT", "Ingeborg-Säßmann"];
const prompt = "Nach dem dritten fehlgeschlagenen Deployment am Freitagnachmittag steht im " +
  "Postmortem-Dokument der Infrastrukturabteilung als Hauptursache, Nebenwirkung und " +
  "einzige Lösung: (1) ____ | (2) ____ | (3) ____ — Donaudampfschifffahrtsgesellschafts" +
  "kapitänsmützenbefestigungsvorschrift inklusive.";
const answers = [
  "Eine Kaffeemaschine, auf der Kubernetes läuft und die jeden Montag ihre eigenen Container neu startet",
  "Ein Praktikant mit Root-Rechten und einem sehr starken Bauchgefühl",
  "Rechtsschutzversicherungsgesellschaftenzusammenschlussverhandlungen",
  "Ein Alarm, der nur an Feiertagen losgeht",
  "Drei verschachtelte YAML-Dateien, die sich gegenseitig überschreiben",
];
const players = names.map((name, i) => ({ seated: true, connected: i !== 5, name, points: i % 3 }));
const game = (phase, extra = {}) => ({
  phase, round: 7, pick: 3, draw: 2, czar: 1, czarName: names[1], promptText: prompt,
  submitted: 4, expected: 7, players, submissions: [], winnerName: "", winningText: "", ...extra,
});
const hand = answers.map(text => ({ text }));
const token = "0123456789abcdef0123456789abcdef";

const viewports = [
  { name: "320", width: 320, height: 568 },
  { name: "390", width: 390, height: 844 },
  { name: "desktop", width: 1280, height: 800 },
];

function fakeSocket() {
  window.__sockets = [];
  class FakeSocket {
    static OPEN = 1;
    constructor(url) {
      this.url = url; this.readyState = 0; this.sent = []; this.handlers = {};
      window.__sockets.push(this);
      setTimeout(() => { this.readyState = 1; this.emit("open", {}); }, 0);
    }
    addEventListener(name, fn) { (this.handlers[name] ||= []).push(fn); }
    emit(name, event) { for (const fn of this.handlers[name] || []) fn(event); }
    send(data) { this.sent.push(data); }
    close() {}
  }
  window.WebSocket = FakeSocket;
  window.__deliver = msg => window.__sockets.at(-1).emit("message", { data: JSON.stringify(msg) });
}

async function noHorizontalScroll(page, label) {
  // On failure, name the innermost elements whose content is wider than their box.
  const { overflow, wide } = await page.evaluate(() => {
    const over = [...document.querySelectorAll("body *")].filter(e => e.scrollWidth > e.clientWidth + 1);
    return {
      overflow: document.documentElement.scrollWidth - window.innerWidth,
      wide: over.filter(e => !over.some(o => o !== e && e.contains(o)))
        .slice(0, 4).map(e => `${e.tagName.toLowerCase()}#${e.id}.${e.className} ${e.scrollWidth}/${e.clientWidth}px`),
    };
  });
  assert.ok(overflow <= 0, `${label}: page scrolls horizontally by ${overflow}px (${wide.join(", ")})`);
}
// Every card shows its whole text: no card is squeezed below its content
// height and none overlaps the next one.
async function cardsIntact(page, listId, label) {
  const bad = await page.$eval(`#${listId}`, list => [...list.children].flatMap((el, i, all) => {
    const r = el.getBoundingClientRect();
    const out = [];
    if (el.scrollHeight > el.clientHeight + 1) out.push(`card ${i + 1} clipped ${el.scrollHeight}/${el.clientHeight}px`);
    if (i + 1 < all.length && r.bottom > all[i + 1].getBoundingClientRect().top + 1) out.push(`card ${i + 1} overlaps ${i + 2}`);
    return out;
  }));
  assert.deepEqual(bad, [], `${label}: ${bad.join(", ")}`);
}
async function inViewport(page, id, label) {
  const r = await page.evaluate(id => {
    const b = document.getElementById(id).getBoundingClientRect();
    return { top: b.top, bottom: b.bottom, left: b.left, right: b.right, h: window.innerHeight, w: window.innerWidth };
  }, id);
  assert.ok(r.top >= 0 && r.bottom <= r.h && r.left >= 0 && r.right <= r.w,
            `${label}: #${id} not fully visible without scrolling (${JSON.stringify(r)})`);
}
async function shot(page, vp, phase) {
  await page.screenshot({ path: path.join(outDir, `${vp.name}-${phase}.png`), fullPage: false });
}

(async () => {
  const browser = await chromium.launch(process.env.CARDPARTY_BROWSER_CHANNEL ?
    { channel: process.env.CARDPARTY_BROWSER_CHANNEL } : {});
  try {
    for (const vp of viewports) {
      const page = await browser.newPage({ viewport: { width: vp.width, height: vp.height },
                                           deviceScaleFactor: 2, hasTouch: vp.name !== "desktop" });
      await page.addInitScript(fakeSocket);
      // Served under the X4 hotspot URL, so location.hostname is realistic.
      await page.route("http://192.168.4.1/**", route =>
        route.fulfill({ status: 200, contentType: "text/html; charset=utf-8", body: html }));
      await page.goto("http://192.168.4.1/cardparty", { waitUntil: "load" });
      await page.waitForFunction(() => window.__sockets && window.__sockets.length === 1);
      const tag = `${vp.name}px`;

      // Join.
      await noHorizontalScroll(page, `${tag} join`);
      await shot(page, vp, "1-join");

      // Lobby with eight long names.
      await page.evaluate(([t, g]) => window.__deliver({ type: "welcome", token: t, revision: 1, game: g,
        private: { isCzar: false, submitted: false, hand: [] } }), [token, game("Lobby", { round: 0 })]);
      assert.equal(await page.isVisible("#lobby"), true);
      await noHorizontalScroll(page, `${tag} lobby`);
      await shot(page, vp, "2-lobby");

      // Pick 3 with a long prompt and long answers.
      await page.evaluate(([g, h]) => window.__deliver({ type: "state", revision: 2, game: g,
        private: { isCzar: false, submitted: false, hand: h } }), [game("Playing"), hand]);
      assert.equal(await page.isVisible("#play"), true);
      await noHorizontalScroll(page, `${tag} play`);
      await inViewport(page, "submit", `${tag} play`);
      const tap = await page.$$eval("#choices .choice", els => Math.min(...els.map(e => e.getBoundingClientRect().height)));
      assert.ok(tap >= 44, `${tag}: answer button only ${tap}px tall`);
      // Scroll the hand, select in reverse order: the scroll position stays and
      // the play order (not the hand index) is shown on each card.
      const before = await page.$eval("#choices", el => { el.scrollTop = el.scrollHeight; return el.scrollTop; });
      for (const i of [4, 2, 0]) {
        await page.$eval("#choices", (el, i) => el.children[i].click(), i);
      }
      const after = await page.$eval("#choices", el => el.scrollTop);
      assert.ok(Math.abs(after - before) <= 1, `${tag}: hand scroll jumped from ${before} to ${after}`);
      const order = await page.$$eval("#choices .position", els => els.map(e => e.textContent.trim()));
      assert.deepEqual(order.sort(), ["<1/3>", "<2/3>", "<3/3>"]);
      const fifth = await page.$eval("#choices", el => el.children[4].textContent);
      assert.ok(fifth.includes("<1/3>"), `${tag}: first pick not marked as position 1`);
      assert.equal(await page.isEnabled("#submit"), true);
      await inViewport(page, "submit", `${tag} play after selection`);
      await cardsIntact(page, "choices", `${tag} play`);
      await shot(page, vp, "3-play-pick3");
      const promptScrolls = await page.$eval(".prompt", el => el.scrollHeight > el.clientHeight);
      if (vp.name === "320") assert.ok(promptScrolls, "320px: long prompt should scroll inside its region");
      await page.click("#submit");
      const sent = await page.evaluate(() => JSON.parse(window.__sockets.at(-1).sent.at(-1)));
      assert.deepEqual(sent.slots, [4, 2, 0]);

      // Judging as Czar: eight anonymous triple answers.
      const submissions = Array.from({ length: 8 }, (_, i) =>
        ({ cards: [answers[i % 5], answers[(i + 1) % 5], answers[(i + 2) % 5]] }));
      await page.evaluate(g => window.__deliver({ type: "state", revision: 3, game: g,
        private: { isCzar: true, submitted: false, hand: [] } }), game("Judging", { submissions }));
      assert.equal(await page.isVisible("#judging"), true);
      await noHorizontalScroll(page, `${tag} judging`);
      await page.$eval("#candidateList", el => { el.scrollTop = el.scrollHeight; el.lastElementChild.click(); });
      assert.equal(await page.isEnabled("#confirmJudge"), true);
      await inViewport(page, "confirmJudge", `${tag} judging`);
      await cardsIntact(page, "candidateList", `${tag} judging`);
      // At least two whole answers fit next to the prompt, even at 320x568.
      const shown = await page.$eval("#candidateList", el => el.clientHeight);
      assert.ok(shown >= 150, `${tag} judging: answer list only ${shown}px tall`);
      await shot(page, vp, "4-judging");

      // Reveal with the winning triple.
      await page.evaluate(g => window.__deliver({ type: "state", revision: 4, game: g,
        private: { isCzar: false, submitted: false, hand: [] } }),
        game("Reveal", { winnerName: names[0], winningText: answers.slice(0, 3).join(" / ") }));
      assert.equal(await page.isVisible("#result"), true);
      await noHorizontalScroll(page, `${tag} reveal`);
      await shot(page, vp, "5-reveal");

      // Paused by a player with the longest name; the timer stays in the header.
      await page.evaluate(g => window.__deliver({ type: "state", revision: 4, game: g,
        private: { isCzar: false, submitted: false, hand: [], pausedByMe: false } }),
        game("Playing", { rounds: 10, clock: "answers", left: 75000, pausedBy: names[0] }));
      assert.equal(await page.isVisible("#hold"), true);
      await noHorizontalScroll(page, `${tag} paused`);
      await inViewport(page, "timer", `${tag} paused`);
      await shot(page, vp, "6-paused");

      // Waiting for a missing player.
      await page.evaluate(g => window.__deliver({ type: "state", revision: 4, game: g,
        private: { isCzar: false, submitted: false, hand: [] } }), game("Judging", { blockedOn: names[2] }));
      assert.equal(await page.isVisible("#hold"), true);
      await noHorizontalScroll(page, `${tag} blocked`);
      await shot(page, vp, "7-blocked");

      // Ceremony: two leaders tied, eight long names.
      const finalPlayers = players.map((p, i) => ({ ...p, points: i === 0 || i === 3 ? 7 : i % 5 }));
      await page.evaluate(g => window.__deliver({ type: "state", revision: 4, game: g,
        private: { isCzar: false, submitted: false, hand: [], voted: false } }),
        game("Finished", { players: finalPlayers, votes: 3, endedByHost: true }));
      assert.equal(await page.isVisible("#final"), true);
      assert.equal(await page.$$eval("#podium .crown", els => els.length), 2);
      await noHorizontalScroll(page, `${tag} final`);
      await inViewport(page, "againButton", `${tag} final`);
      await shot(page, vp, "8-final");

      // Removed by the host.
      await page.evaluate(() => window.__deliver({ type: "kicked" }));
      assert.equal(await page.isVisible("#removed"), true);
      await noHorizontalScroll(page, `${tag} removed`);
      await shot(page, vp, "9-removed");
      await page.close();
      console.log(`PASS cardparty viewport ${vp.width}x${vp.height}: join, lobby, Pick 3, judging, reveal, pause, block, final, removed`);
    }
  } finally {
    await browser.close();
  }
  console.log(`cardparty viewport screenshots: ${path.relative(root, outDir)}/`);
})().catch(error => { console.error(error); process.exit(1); });
