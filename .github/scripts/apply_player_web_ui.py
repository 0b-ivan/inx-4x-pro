from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"pattern not found in {path}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


# Complete the reusable web adapter with the visible game identity.
replace_once(
    "src/apps_local/player/PlayerWebApi.cpp",
    "\n}  // namespace playerweb\n",
    '''
std::string displayName() {
  player::PlayerRuntime& runtime = player::runtime();
  if (!runtime.ready()) runtime.begin();
  if (const player::Player* active = runtime.activePlayer()) return active->name;
  if (runtime.ready() && runtime.guest().active()) {
    char callsign[player::kMaxNameLength + 1]{};
    player::compose(callsign, sizeof(callsign), runtime.guest().callsign);
    if (callsign[0] != '\\0') return callsign;
  }
  return "X4 PRO";
}

}  // namespace playerweb
''',
)

# Player HTTP endpoints live in the existing Battleship server so no second
# listener/session stack is created. PlayerWebApi itself stays game-neutral.
replace_once(
    "src/apps_local/battleship/web/BattleshipBrowserServer.cpp",
    '#include "../../../DevMode.h"\n#include "BattleshipPageHtml.generated.h"',
    '#include "../../../DevMode.h"\n#include "../../player/PlayerWebApi.h"\n#include "BattleshipPageHtml.generated.h"',
)
replace_once(
    "src/apps_local/battleship/web/BattleshipBrowserServer.cpp",
    'constexpr const char* kGamePath = "/battleship";\n',
    '''constexpr const char* kGamePath = "/battleship";

bool playerAuthorized(WebServer& http, const char* token) {
  return token != nullptr && token[0] != '\\0' && http.hasArg("token") && http.arg("token") == token;
}

void sendPlayerReply(WebServer& http, const playerweb::Reply& reply) {
  http.sendHeader("Cache-Control", "no-store");
  http.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
  http.send(reply.status, "application/json", reply.body.c_str());
}
''',
)
replace_once(
    "src/apps_local/battleship/web/BattleshipBrowserServer.cpp",
    '  rotateToken();\n  rotateResumeToken();\n  mode_ = mode;',
    '  rotateToken();\n  rotateResumeToken();\n  opponentName_ = playerweb::displayName();\n  mode_ = mode;',
)
replace_once(
    "src/apps_local/battleship/web/BattleshipBrowserServer.cpp",
    '''  http_.on("/battleship/session", HTTP_GET, [this] {
    http_.sendHeader("Cache-Control", "no-store");
    http_.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
    http_.send(200, "text/plain", token_);
  });
  http_.onNotFound([this] { handleNotFound(); });''',
    '''  http_.on("/battleship/session", HTTP_GET, [this] {
    http_.sendHeader("Cache-Control", "no-store");
    http_.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
    http_.send(200, "text/plain", token_);
  });
  http_.on("/player/state", HTTP_GET, [this] {
    if (!playerAuthorized(http_, token_)) {
      sendPlayerReply(http_, playerweb::Reply{403, "{\\\"ok\\\":false,\\\"message\\\":\\\"FORBIDDEN\\\"}"});
      return;
    }
    sendPlayerReply(http_, playerweb::state());
  });
  http_.on("/player/action", HTTP_POST, [this] {
    if (!playerAuthorized(http_, token_)) {
      sendPlayerReply(http_, playerweb::Reply{403, "{\\\"ok\\\":false,\\\"message\\\":\\\"FORBIDDEN\\\"}"});
      return;
    }
    if (snapshot_.phase == BrowserPhase::Playing) {
      sendPlayerReply(http_, playerweb::Reply{409, "{\\\"ok\\\":false,\\\"message\\\":\\\"PLAYER LOCKED DURING MATCH\\\"}"});
      return;
    }

    const String action = http_.arg("action");
    playerweb::Reply reply;
    if (action == "guest") {
      reply = playerweb::useGuest();
    } else if (action == "login") {
      reply = playerweb::login(http_.arg("id").c_str(), http_.arg("pin").c_str());
    } else if (action == "register") {
      reply = playerweb::registerGuest(http_.arg("name").c_str(), http_.arg("pin").c_str());
    } else if (action == "callsign") {
      reply = playerweb::stepGuestCallsign(http_.arg("slot").toInt());
    } else {
      reply = playerweb::Reply{400, "{\\\"ok\\\":false,\\\"message\\\":\\\"UNKNOWN ACTION\\\"}"};
    }

    if (reply.status == 200) {
      opponentName_ = playerweb::displayName();
      const int n = std::snprintf(reply_, sizeof(reply_), "{\\\"type\\\":\\\"peer\\\",\\\"name\\\":\\\"%s\\\"}",
                                  opponentName_.c_str());
      if (n > 0 && static_cast<size_t>(n) < sizeof(reply_)) ws_.broadcastTXT(reply_, static_cast<size_t>(n));
    }
    sendPlayerReply(http_, reply);
  });
  http_.onNotFound([this] { handleNotFound(); });''',
)

page = Path("src/apps_local/battleship/web/BattleshipPage.html")
text = page.read_text()
css = r'''
      #devicePlayer { margin:18px 0; border:2px solid var(--line); padding:12px; }
      #devicePlayer h2 { margin:0 0 8px; }
      .device-player-head { display:flex; justify-content:space-between; gap:10px; align-items:start; }
      .device-player-name { font-size:20px; font-weight:900; }
      .device-player-mode { color:var(--muted); font-size:12px; text-transform:uppercase; }
      .profile-grid { display:grid; grid-template-columns:repeat(3,1fr); gap:6px; margin:10px 0; }
      .metric { border:1px solid var(--line); padding:7px; min-width:0; }
      .metric b { display:block; font-size:17px; overflow-wrap:anywhere; }
      .metric span { font-size:10px; color:var(--muted); }
      .radar-bars { display:grid; gap:4px; margin:10px 0; }
      .radar-row { display:grid; grid-template-columns:88px 1fr 34px; align-items:center; gap:6px; font-size:11px; }
      .radar-track { height:10px; border:1px solid var(--line); }
      .radar-fill { height:100%; background:var(--ink); }
      .player-controls { display:grid; gap:8px; margin-top:10px; }
      .player-controls input,.player-controls select { width:100%; min-height:40px; font:inherit; background:var(--panel); color:var(--ink); border:1px solid var(--line); padding:8px; }
      .player-inline { display:grid; grid-template-columns:1fr auto; gap:7px; }
      .callsign-actions { display:grid; grid-template-columns:repeat(3,1fr); gap:6px; }
      #playerMessage { min-height:18px; margin:7px 0 0; font-size:12px; font-weight:700; }
      main.in-battle #devicePlayer { display:none; }
'''
if "#devicePlayer {" not in text:
    text = text.replace("    </style>", css + "    </style>", 1)

panel = r'''
      <section id="devicePlayer">
        <div class="device-player-head">
          <div><p class="eyebrow">DEVICE PROFILE</p><div id="devicePlayerName" class="device-player-name">Loading…</div><div id="deviceCallsign" class="hint"></div></div>
          <div id="devicePlayerMode" class="device-player-mode"></div>
        </div>
        <div id="deviceMetrics" class="profile-grid"></div>
        <div id="deviceRadar" class="radar-bars"></div>
        <div class="player-controls">
          <div class="player-inline"><select id="savedPlayer"></select><button id="useGuestPlayer" type="button">GUEST</button></div>
          <div class="player-inline"><input id="playerPin" type="password" inputmode="numeric" maxlength="4" placeholder="4-digit PIN" /><button id="loginPlayer" type="button">LOGIN</button></div>
          <div id="guestCallsignActions" class="callsign-actions"><button data-slot="0" type="button">HAIR +</button><button data-slot="1" type="button">EYES +</button><button data-slot="2" type="button">MOUTH +</button></div>
          <div class="player-inline"><input id="registerName" maxlength="20" placeholder="New player name" /><input id="registerPin" type="password" inputmode="numeric" maxlength="4" placeholder="4-digit PIN" /></div>
          <button id="registerPlayer" type="button">REGISTER GUEST</button>
        </div>
        <p id="playerMessage" role="status"></p>
      </section>
'''
marker = '      <p id="setupState" class="copy">Device status: <code id="phase">offline</code><br /><span id="turn"></span></p>\n'
if 'id="devicePlayer"' not in text:
    if marker not in text:
        raise SystemExit("setupState marker not found")
    text = text.replace(marker, marker + panel, 1)

js = r'''

      // Device-wide player UI. The X4 remains authoritative; this client only
      // requests identity actions and renders the returned derived profile.
      let pxTokenValue = '';
      const pxLabels = ['STRATEGY','TACTICS','PRECISION','RISK','ENDURANCE','VERSATILITY'];
      async function pxToken() {
        if (pxTokenValue) return pxTokenValue;
        const response = await fetch('/battleship/session', {cache:'no-store'});
        pxTokenValue = (await response.text()).trim();
        return pxTokenValue;
      }
      async function pxState() {
        try {
          const token = await pxToken();
          const response = await fetch('/player/state?token=' + encodeURIComponent(token), {cache:'no-store'});
          const data = await response.json();
          pxRender(data);
        } catch (_) {
          document.getElementById('playerMessage').textContent = 'PLAYER API OFFLINE';
        }
      }
      async function pxAction(action, fields={}) {
        const token = await pxToken();
        const body = new URLSearchParams({token, action, ...fields});
        const response = await fetch('/player/action', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body});
        const data = await response.json();
        pxRender(data);
        return data;
      }
      function pxRender(data) {
        const current = data.current || {};
        const profile = data.profile || {};
        const battleship = profile.battleship || {};
        document.getElementById('devicePlayerName').textContent = current.name || 'PLAYER OFFLINE';
        document.getElementById('deviceCallsign').textContent = current.callsign || '';
        document.getElementById('devicePlayerMode').textContent = data.mode === 'guest-only' ? 'STORAGE OFFLINE · GUEST ONLY' : (current.registered ? 'REGISTERED' : 'GUEST');
        document.getElementById('playerMessage').textContent = data.message || (data.mode === 'guest-only' ? 'Player database unavailable; guest progress remains in RAM.' : '');

        const metrics = [
          ['LEVEL', profile.level ?? 1], ['XP', profile.xp ?? 0], ['CLASS', profile.class || 'ALLROUNDER'],
          ['BATTLESHIP', battleship.rank || 'NO RANK'], ['W / L / D', `${battleship.wins||0} / ${battleship.losses||0} / ${battleship.draws||0}`], ['BEST STREAK', battleship.bestStreak||0]
        ];
        document.getElementById('deviceMetrics').innerHTML = metrics.map(([label,value]) => `<div class="metric"><b>${String(value)}</b><span>${label}</span></div>`).join('');
        const radar = Array.isArray(profile.radar) ? profile.radar : [0,0,0,0,0,0];
        document.getElementById('deviceRadar').innerHTML = pxLabels.map((label,i) => `<div class="radar-row"><span>${label}</span><div class="radar-track"><div class="radar-fill" style="width:${Math.max(0,Math.min(100,radar[i]||0))}%"></div></div><b>${radar[i]||0}</b></div>`).join('');

        const select = document.getElementById('savedPlayer');
        const previous = select.value;
        select.replaceChildren();
        (data.players || []).forEach(player => { const option=document.createElement('option'); option.value=player.id; option.textContent=player.name; select.appendChild(option); });
        if ([...select.options].some(option => option.value === previous)) select.value = previous;
        const persistent = !!data.persistence;
        document.getElementById('loginPlayer').disabled = !persistent || !select.value;
        document.getElementById('playerPin').disabled = !persistent || !select.value;
        document.getElementById('registerPlayer').disabled = !current.canRegister;
        document.getElementById('registerName').disabled = !current.canRegister;
        document.getElementById('registerPin').disabled = !current.canRegister;
        document.getElementById('guestCallsignActions').hidden = !!current.registered;
      }
      document.getElementById('useGuestPlayer').addEventListener('click', () => pxAction('guest'));
      document.getElementById('loginPlayer').addEventListener('click', async () => {
        const select=document.getElementById('savedPlayer'); const pin=document.getElementById('playerPin');
        await pxAction('login', {id:select.value, pin:pin.value}); pin.value='';
      });
      document.getElementById('registerPlayer').addEventListener('click', async () => {
        const name=document.getElementById('registerName'); const pin=document.getElementById('registerPin');
        await pxAction('register', {name:name.value.trim(), pin:pin.value}); pin.value='';
      });
      document.querySelectorAll('#guestCallsignActions button').forEach(button => button.addEventListener('click', () => pxAction('callsign', {slot:button.dataset.slot})));
      pxState();
      setInterval(() => { if (!document.getElementById('app').classList.contains('in-battle')) pxState(); }, 5000);
'''
if "let pxTokenValue" not in text:
    idx = text.rfind("    </script>")
    if idx < 0:
        raise SystemExit("script closing tag not found")
    text = text[:idx] + js + text[idx:]
page.write_text(text)
