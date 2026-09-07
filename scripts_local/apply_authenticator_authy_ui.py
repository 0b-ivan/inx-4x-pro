#!/usr/bin/env python3
from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one exact match, got {count}")
    p.write_text(text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    new, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex match, got {count}")
    p.write_text(new)


# ---------------------------------------------------------------------------
# Device UI: compact Authy-style account list + long-press delete context.
# ---------------------------------------------------------------------------
replace_once(
    "src/apps_local/totp/TotpActivity.h",
    """  std::array<freeink::ui::ListItem, kMaxAccounts> listRows_{};\n  uint64_t shownCounter_ = 0;\n""",
    """  std::array<freeink::ui::ListItem, kMaxAccounts> listRows_{};\n  std::array<std::array<char, 64>, kMaxAccounts> listLabels_{};\n  int16_t listHitTop_ = 0;\n  int16_t listHitRowHeight_ = 0;\n  int16_t listHitRowStep_ = 0;\n  int16_t listHitXStart_ = 0;\n  int16_t listHitXEnd_ = 0;\n  int listHitVisibleRows_ = 0;\n  uint64_t shownCounter_ = 0;\n""",
)

replace_once(
    "src/apps_local/totp/TotpActivity.cpp",
    """fui::TextStyle centered(const fui::TextStyle& base, const uint8_t maxLines = 1) {\n  fui::TextStyle style = base;\n  style.align = fui::TextAlign::Center;\n  style.maxLines = maxLines;\n  return style;\n}\n\n""",
    """fui::TextStyle centered(const fui::TextStyle& base, const uint8_t maxLines = 1) {\n  fui::TextStyle style = base;\n  style.align = fui::TextAlign::Center;\n  style.maxLines = maxLines;\n  return style;\n}\n\nbool formatTotpCode(const char* secret, const uint8_t digits, const uint16_t period, const uint64_t now,\n                    char* out, const size_t outSize) {\n  if (out == nullptr || outSize == 0 || secret == nullptr || period == 0) return false;\n  out[0] = '\\0';\n  bool ok = false;\n  const uint32_t code = totp::generate(secret, now, digits, period, &ok);\n  if (!ok) return false;\n\n  char raw[12]{};\n  std::snprintf(raw, sizeof(raw), \"%0*u\", static_cast<int>(digits), static_cast<unsigned>(code));\n  const int split = static_cast<int>(digits / 2);\n  std::snprintf(out, outSize, \"%.*s %s\", split, raw, raw + split);\n  return true;\n}\n\n""",
)

regex_once(
    "src/apps_local/totp/TotpActivity.cpp",
    r"  if \(phase_ == Phase::List\) \{\n    const bool next = mappedInput\.wasReleased\(MappedInputManager::Button::Down\);.*?\n  \}\n\n  if \(phase_ == Phase::Code && selected_ >= 0",
    """  if (phase_ == Phase::List) {\n    int holdX = 0;\n    int holdY = 0;\n    if (mappedInput.wasScreenLongPress(holdX, holdY) && listHitVisibleRows_ > 0 && listHitRowStep_ > 0 &&\n        holdX >= listHitXStart_ && holdX < listHitXEnd_ && holdY >= listHitTop_) {\n      const int localY = holdY - listHitTop_;\n      const int row = localY / listHitRowStep_;\n      const int withinRow = localY % listHitRowStep_;\n      if (row >= 0 && row < listHitVisibleRows_ && withinRow < listHitRowHeight_) {\n        const int index = topIndex_ + row;\n        if (index >= 0 && index < static_cast<int>(store_.count)) {\n          selected_ = index;\n          phase_ = Phase::ConfirmDelete;\n          requestUpdate();\n          return;\n        }\n      }\n    }\n\n    const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);\n    const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);\n    const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();\n    if (next || swipe == MappedInputManager::SwipeDir::Up) {\n      pageList(1);\n      return;\n    }\n    if (prev || swipe == MappedInputManager::SwipeDir::Down) {\n      pageList(-1);\n      return;\n    }\n\n    if (store_.count > 0 && clockValid()) {\n      const uint64_t window = static_cast<uint64_t>(std::time(nullptr)) / 30U;\n      if (window != shownCounter_) {\n        requestUpdate();\n        return;\n      }\n    }\n  }\n\n  if (phase_ == Phase::Code && selected_ >= 0""",
)

replace_once(
    "src/apps_local/totp/TotpActivity.cpp",
    """  const toybox::Faces faces = phase_ == Phase::Code ? toybox::bigNumberFaces() : toybox::toyboxFaces();\n""",
    """  const toybox::Faces faces = toybox::toyboxFaces();\n""",
)

regex_once(
    "src/apps_local/totp/TotpActivity.cpp",
    r"    case Phase::List: \{.*?\n      break;\n    \}\n\n    case Phase::Code:",
    """    case Phase::List: {\n      char countLabel[16];\n      std::snprintf(countLabel, sizeof(countLabel), \"%u / %d\", static_cast<unsigned>(store_.count), kMaxAccounts);\n      chrome(screen, \"AUTHENTICATOR\", countLabel);\n\n      fui::ButtonProps add;\n      add.label = store_.count < kMaxAccounts ? \"+ ADD ACCOUNT\" : \"FULL\";\n      add.action = kActionAdd;\n      add.styles = toybox::rowStyles();\n      screen.button(add, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));\n\n      const fui::Rect body = screen.body();\n      const int16_t listHeight = static_cast<int16_t>(footerY - toybox::kGutter - body.y);\n      listHitTop_ = 0;\n      listHitRowHeight_ = 0;\n      listHitRowStep_ = 0;\n      listHitXStart_ = 0;\n      listHitXEnd_ = 0;\n      listHitVisibleRows_ = 0;\n\n      if (store_.count == 0) {\n        target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + toybox::kMargin * 2), width, 120),\n                    \"NO ACCOUNTS\\nAdd a TOTP account from the device or web interface.\",\n                    centered(screen.theme().bodyText, 3));\n        visibleRows_ = 0;\n        topIndex_ = 0;\n        shownCounter_ = 0;\n        shownClockValid_ = clockValid();\n      } else {\n        const bool validClock = clockValid();\n        const uint64_t now = validClock ? static_cast<uint64_t>(std::time(nullptr)) : 0;\n\n        for (uint16_t i = 0; i < store_.count; ++i) {\n          listRows_[i] = fui::ListItem{};\n          char code[16]{};\n          if (!validClock || !formatTotpCode(store_.accounts[i].secret, store_.accounts[i].digits,\n                                             store_.accounts[i].period, now, code, sizeof(code))) {\n            std::snprintf(code, sizeof(code), \"CLOCK NOT SET\");\n          }\n\n          const char* name = store_.accounts[i].name;\n          constexpr int kNameChars = 25;\n          const bool clipped = std::strlen(name) > kNameChars;\n          std::snprintf(listLabels_[i].data(), listLabels_[i].size(), \"%.*s%s\\n%s\", kNameChars, name,\n                        clipped ? \"...\" : \"\", code);\n          listRows_[i].label = listLabels_[i].data();\n          listRows_[i].value = \"\";\n          listRows_[i].actionValue = static_cast<int16_t>(i);\n        }\n\n        fui::ListProps list;\n        list.items = listRows_.data();\n        list.count = store_.count;\n        list.topIndex = static_cast<uint16_t>(topIndex_);\n        list.selectedIndex = -1;\n        list.action = kActionOpen;\n        list.rowHeight = 88;\n        list.labelText = screen.theme().bodyText;\n        list.labelText.maxLines = 2;\n        list.valueText = screen.theme().smallText;\n        list.balanceWrappedLabelWithValue = false;\n\n        visibleRows_ = fui::listVisibleRows(fui::makeRect(body.x, body.y, body.width, listHeight), list.rowHeight,\n                                            screen.theme().listRowGap);\n        if (visibleRows_ > 0) {\n          const int maxTop = ((static_cast<int>(store_.count) - 1) / visibleRows_) * visibleRows_;\n          if (topIndex_ > maxTop) topIndex_ = maxTop;\n        }\n\n        listHitTop_ = body.y;\n        listHitRowHeight_ = list.rowHeight;\n        listHitRowStep_ = static_cast<int16_t>(list.rowHeight + screen.theme().listRowGap);\n        listHitXStart_ = body.x;\n        listHitXEnd_ = static_cast<int16_t>(body.x + body.width);\n        listHitVisibleRows_ = visibleRows_;\n\n        screen.list(list, listHeight, fui::LayoutAnchor::Top);\n        shownClockValid_ = validClock;\n        shownCounter_ = validClock ? now / 30U : 0;\n      }\n      break;\n    }\n\n    case Phase::Code:""",
)

replace_once(
    "src/apps_local/totp/TotpActivity.cpp",
    """          fui::TextStyle codeStyle = centered(screen.theme().bodyText);\n          target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 90), width, 150), shown, codeStyle);\n""",
    """          fui::TextStyle codeStyle = centered(screen.theme().bodyText);\n          codeStyle.font = fui::FONT_SLOT_TITLE;\n          codeStyle.color = fui::Color::Black;\n          target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 105), width, 90), shown, codeStyle);\n""",
)

replace_once(
    "src/apps_local/totp/TotpActivity.cpp",
    """      fui::ButtonProps remove;\n      remove.label = \"DELETE ACCOUNT\";\n      remove.action = kActionDelete;\n      remove.styles = toybox::rowStyles();\n      screen.button(remove, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));\n      break;\n""",
    """      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(footerY - 70), width, 48),\n                  \"Hold an account in the list to delete it.\", centered(screen.theme().smallText, 2));\n      break;\n""",
)

replace_once(
    "src/apps_local/totp/TotpActivity.cpp",
    """  const auto labels = mappedInput.mapLabels(\"Back\", \"\", \"Up\", \"Down\");\n""",
    """  const auto labels = phase_ == Phase::List ? mappedInput.mapLabels(\"Back\", \"\", \"Up\", \"Down\")\n                                                : mappedInput.mapLabels(\"Back\", \"\", \"\", \"\");\n""",
)

# ---------------------------------------------------------------------------
# Web API: return current TOTP values without ever returning Base32 secrets.
# ---------------------------------------------------------------------------
replace_once(
    "src/network/CrossPointWebServer.cpp",
    """#include <cctype>\n#include <cstring>\n""",
    """#include <cctype>\n#include <cstdio>\n#include <cstring>\n#include <ctime>\n""",
)

replace_once(
    "src/network/CrossPointWebServer.cpp",
    """  JsonDocument out; out[\"created\"] = created; JsonArray arr = out[\"accounts\"].to<JsonArray>();\n  for (uint16_t i=0;i<store.count;++i) {\n    JsonObject a=arr.add<JsonObject>(); a[\"index\"]=i; a[\"name\"]=store.accounts[i].name; a[\"digits\"]=store.accounts[i].digits; a[\"period\"]=store.accounts[i].period;\n  }\n  String body; serializeJson(out, body); server->send(200, \"application/json\", body);\n""",
    """  JsonDocument out;\n  out[\"created\"] = created;\n  const uint64_t now = static_cast<uint64_t>(std::time(nullptr));\n  const bool clockValid = now > 1700000000ULL;\n  out[\"serverTime\"] = now;\n  out[\"clockValid\"] = clockValid;\n  JsonArray arr = out[\"accounts\"].to<JsonArray>();\n  for (uint16_t i = 0; i < store.count; ++i) {\n    const WebTotpAccount& account = store.accounts[i];\n    JsonObject a = arr.add<JsonObject>();\n    a[\"index\"] = i;\n    a[\"name\"] = account.name;\n    a[\"digits\"] = account.digits;\n    a[\"period\"] = account.period;\n\n    char shown[16]{};\n    uint32_t remaining = 0;\n    if (clockValid && account.period > 0) {\n      bool ok = false;\n      const uint32_t code = totp::generate(account.secret, now, account.digits, account.period, &ok);\n      if (ok) {\n        char raw[12]{};\n        std::snprintf(raw, sizeof(raw), \"%0*u\", static_cast<int>(account.digits), static_cast<unsigned>(code));\n        const int split = static_cast<int>(account.digits / 2);\n        std::snprintf(shown, sizeof(shown), \"%.*s %s\", split, raw, raw + split);\n        remaining = static_cast<uint32_t>(account.period - (now % account.period));\n      }\n    }\n    a[\"code\"] = shown;\n    a[\"remaining\"] = remaining;\n    a[\"expiresAt\"] = remaining > 0 ? now + remaining : 0;\n  }\n  String body;\n  serializeJson(out, body);\n  std::memset(&store, 0, sizeof(store));\n  server->send(200, \"application/json\", body);\n""",
)

# ---------------------------------------------------------------------------
# Web UI: Authy-style cards, live local countdown, refresh only at code rollover.
# ---------------------------------------------------------------------------
Path("src/network/html/AuthenticatorPage.html").write_text(r'''<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>CrossPlay Authenticator</title>
<style>
:root{--font:#26323d;--muted:#6d7882;--bg:#f4f5f7;--title:#2c3e50;--card:#fff;--border:#dfe3e8;--accent:rgb(110,154,130);--danger:#b33;--track:#e5e9ed}
@media(prefers-color-scheme:dark){:root{--font:#f5f5f5;--muted:#b8c0c7;--bg:#303030;--title:#ecf0f1;--card:#444;--border:#62676c;--track:#565b60;color-scheme:dark}}
*{box-sizing:border-box}body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;max-width:800px;margin:auto;padding:20px;background:var(--bg);color:var(--font)}
h1,h2{color:var(--title)}h1{border-bottom:2px solid var(--accent);padding-bottom:10px}.nav{display:flex;gap:10px;flex-wrap:wrap;margin:20px 0}.nav a{padding:10px 16px;text-decoration:none;color:var(--font);border-radius:4px}.nav a.active{background:var(--accent);color:white}
.panel{background:var(--card);padding:20px;border-radius:12px;margin:15px 0;box-shadow:0 2px 8px #0002}label{display:block;font-weight:600;margin-top:10px}input{width:100%;padding:10px;margin-top:5px;border:1px solid var(--border);border-radius:6px;background:transparent;color:var(--font)}button{margin-top:12px;padding:10px 16px;border:0;border-radius:6px;background:var(--accent);color:white;cursor:pointer}.danger{background:transparent;color:var(--danger);border:1px solid var(--border);margin:0}.muted{color:var(--muted);font-size:.9em}.error{color:#d44}.ok{color:#2a8b57}
.accounts{display:grid;gap:14px}.otp-card{background:var(--card);border:1px solid var(--border);border-radius:18px;padding:17px 20px;box-shadow:0 2px 9px #0001}.otp-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.otp-name{font-size:1.05rem;font-weight:700;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.otp-code{font-size:2rem;letter-spacing:.09em;font-variant-numeric:tabular-nums;margin-top:4px}.otp-meta{display:flex;align-items:center;justify-content:space-between;gap:14px;margin-top:12px}.otp-time{min-width:38px;text-align:right;font-variant-numeric:tabular-nums;color:var(--muted)}.progress{height:4px;background:var(--track);border-radius:999px;overflow:hidden;flex:1}.progress-fill{height:100%;width:100%;background:var(--accent);transition:width .2s linear}.empty{padding:12px 0;color:var(--muted)}
@media(max-width:560px){body{padding:12px}.otp-code{font-size:1.65rem}.panel{padding:16px}}
</style>
</head>
<body>
<h1>CrossPlay</h1>
<div class="nav"><a href="/">Home</a><a href="/files">File Manager</a><a href="/settings">Settings</a><a href="/fonts">Fonts</a><a class="active" href="/authenticator">Authenticator</a></div>
<div class="panel">
<h2>TOTP Vault</h2>
<p class="muted">The reader generates the codes. Base32 secrets are never returned to this page. PIN and new secrets still travel over your local HTTP connection, so only use this on a trusted network.</p>
<label>Vault PIN</label><input id="pin" type="password" inputmode="numeric" minlength="6" maxlength="12" placeholder="6-12 digits" />
<button onclick="loadAccounts(false)">Unlock / Refresh</button><div id="status" class="muted"></div>
</div>
<div class="panel"><h2>Accounts</h2><div id="accounts" class="accounts"><div class="empty">Unlock the vault to load current TOTP codes.</div></div></div>
<div class="panel">
<h2>Add account</h2><label>Name</label><input id="name" maxlength="39" placeholder="e.g. GitHub" /><label>Base32 secret</label><input id="secret" type="password" maxlength="96" autocomplete="off" placeholder="JBSWY3DPEHPK3PXP" /><button onclick="addAccount()">Add account</button>
</div>
<script>
let serverEpoch=0,receivedAt=0,refreshing=false,unlocked=false;
function pin(){return document.getElementById('pin').value.trim()}
function setStatus(msg,ok){const e=document.getElementById('status');e.textContent=msg;e.className=ok?'ok':'error'}
async function post(url,data){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const t=await r.text();if(!r.ok)throw new Error(t||('HTTP '+r.status));return t?JSON.parse(t):{}}
function estimatedNow(){return serverEpoch+(Date.now()-receivedAt)/1000}
function renderAccounts(d){const box=document.getElementById('accounts');box.innerHTML='';if(!d.accounts.length){box.innerHTML='<div class="empty">No accounts yet.</div>';return}d.accounts.forEach(a=>{const card=document.createElement('div');card.className='otp-card';card.dataset.expires=String(a.expiresAt||0);card.dataset.period=String(a.period||30);card.innerHTML='<div class="otp-head"><div style="min-width:0"><div class="otp-name"></div><div class="otp-code"></div></div></div><div class="otp-meta"><div class="progress"><div class="progress-fill"></div></div><span class="otp-time"></span><button class="danger" type="button">Delete</button></div>';card.querySelector('.otp-name').textContent=a.name;card.querySelector('.otp-code').textContent=a.code||'CLOCK NOT SET';card.querySelector('.danger').onclick=()=>del(a.index);box.appendChild(card)});tick()}
async function loadAccounts(silent){if(refreshing)return;refreshing=true;try{const d=await post('/api/totp/list',{pin:pin()});serverEpoch=Number(d.serverTime||0);receivedAt=Date.now();unlocked=true;renderAccounts(d);if(!silent)setStatus(d.created?'New encrypted vault created.':'Vault unlocked.',true)}catch(e){unlocked=false;if(!silent)setStatus(e.message,false)}finally{refreshing=false}}
function tick(){if(!unlocked)return;const now=estimatedNow();let expired=false;document.querySelectorAll('.otp-card').forEach(card=>{const expires=Number(card.dataset.expires||0),period=Math.max(1,Number(card.dataset.period||30));const remaining=expires?Math.max(0,Math.ceil(expires-now)):0;card.querySelector('.otp-time').textContent=expires?remaining+'s':'';card.querySelector('.progress-fill').style.width=expires?Math.max(0,Math.min(100,remaining/period*100))+'%':'0%';if(expires&&remaining<=0)expired=true});if(expired&&!refreshing)loadAccounts(true)}
async function addAccount(){try{await post('/api/totp',{pin:pin(),name:document.getElementById('name').value.trim(),secret:document.getElementById('secret').value.trim()});document.getElementById('secret').value='';await loadAccounts(true);setStatus('Account added.',true)}catch(e){setStatus(e.message,false)}}
async function del(index){if(!confirm('Delete this TOTP account?'))return;try{await post('/api/totp/delete',{pin:pin(),index:index});await loadAccounts(true);setStatus('Account deleted.',true)}catch(e){setStatus(e.message,false)}}
setInterval(tick,500);
</script>
</body></html>''')

# ---------------------------------------------------------------------------
# Release v1.12.24 in the same final main push.
# ---------------------------------------------------------------------------
replace_once("platformio.ini", "[crossplay]\nversion = 1.12.23", "[crossplay]\nversion = 1.12.24")

regex_once(
    ".github/workflows/crossplay-release.yml",
    r"            ### What is new in 1\.12\.23\n\n.*?            - No OTA, partition-table or installer contract changes\.\n",
    """            ### What is new in 1.12.24\n\n            - Reworks the **Authenticator** into a compact multi-account TOTP\n              list instead of the oversized single-code presentation.\n            - A short tap still opens a code; a **long press on a list entry**\n              opens the delete confirmation context.\n            - The X4 Pro web interface now shows current TOTP codes in Authy-style\n              cards with a local countdown/progress indicator after vault unlock.\n            - TOTP secrets stay on the reader: the web API returns generated codes\n              and expiry metadata, never the stored Base32 secret.\n            - Keeps the encrypted AES-256-GCM/PBKDF2 vault and X4 Pro-only release line.\n            - No OTA, partition-table or installer contract changes.\n""",
)

print("Authenticator Authy-style UI patch applied")
