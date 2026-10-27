# CrossPlay Terminal — NODE-inspired design system

> Design proposal v0.1 · 2026-10-27 · not yet a global firmware theme
>
> Reference: https://www.youtube.com/@NODEtv/videos — https://n-o-d-e.net/
>
> First pilot: [Cards Against Whatever](apps/cards-against-whatever.md)
>
> Existing design foundations: [Toybox](design-language.md), [CrossPlay identity](identity.md)

## 1. Vision and boundaries

CrossPlay Terminal is the **long-term visual direction for the entire X4 Pro
firmware**, including menus, settings, games and local multiplayer browser
clients. It borrows NODE's restrained black/white, text-first hacker and BBS
aesthetic. It does not copy NODE branding, logos, footage, original assets or
exact layouts.

**A terminal that is pleasant to operate, not a terminal simulator.**
Every label or rule must clarify where we are, what is happening, or what to do.

- Pure monochrome UI, no colored accents, grays, gradients, glow, CRT filters,
  shadows, rounded cards, ornamental illustrations or fake loading animations.
- Legible monospace for application chrome, deliberately spaced metadata,
  thin separators, numbered commands and text-first information hierarchy.
- Typography carries the hierarchy: short CAPS labels, normally cased prose,
  measured sizes and explicit selection state.
- Touch-first: actual tappable rows, not a mandatory command line.
- Stillness on e-ink: redraw only when something meaningful changes.
- Share *semantics and components* across screen technologies, not necessarily
  identical inverted polarity.
- No big-bang replacement: keep the existing reader and Toybox functional
  while piloting the new theme in isolated apps.

The X4's native display is **800×480 landscape, 1-bit e-ink**. Do not assume
portrait orientation without implementing and testing display/touch rotation.
Keep EPUB page typography and content rendering intact. Terminal can redesign
reading menus and chrome, but must not compromise actual book reading.

## 2. Visual identity and tokens

| Token | Phone/web: TERMINAL DARK | X4: TERMINAL PAPER |
| --- | --- | --- |
| surface | #000000 | White |
| ink / borders | #ffffff | Black |
| selected | White fill + black text | Black fill + white text, sparingly |
| default divider | 1px line | 1px line |
| strong divider / focus | 2px line | 2px line |
| corner radius | 0 | 0 |
| status | CONN / WAIT / ERROR as text | Same meaning and translation |
| progress | 3/5 RECEIVED, optionally segments | Numeral plus sparse progress marks |

A disabled control retains readable contrast and an explicit DISABLED or
unavailable reason. Never encode status only through inversion/color/hover.

### Spacing — provisional tokens

- Unit: 4 CSS px on web / 4 physical px on X4.
- Page gutter: 16–24 CSS px on phones, about 24 px on X4.
- Row gap: 8–12 px; section spacing: 16–24 px.
- Touch targets: **minimum 44×44 CSS px** on phones, equivalent tested touch
  affordance on X4. Outlines and labels can be thinner than the hit target.
- Scrolling is vertical and predictable, without hidden horizontal overflow.
- All numbers are *starting points*; verify with font metrics and hardware.

### Typography — provisional scale

Preferred web stack: ui-monospace, Menlo, SFMono-Regular, Consolas,
Liberation Mono, monospace. **No downloadable font dependencies.**

| Role | Phone | Native X4 target |
| --- | --- | --- |
| Short metadata | 12–13px | 14–16px |
| Row / control | 16–18px | 20–24px |
| Prompt / headline | 22–30px | 28–36px |
| Large score | 26–40px | 34–52px |

The native targets require an actual font audit. Existing Toybox fonts may be
used during transition; do not assume Menlo is installed on the microcontroller.
Use the *largest legible cut that fits*, then wrap/scroll, never shrink to an
illegible font or silently clip important content. Preserve ä, ö, ü, ß, Unicode
punctuation and locale-specific line breaks.

## 3. Screen grammar

There are four optional zones, each with one job:

1. **Masthead** — app shortmark, location, live session/status.
2. **Context** — page title or current question.
3. **Work area** — numbered controls, cards, board or content.
4. **Action footer** — progress plus next valid action.

Example structural wireframe (not an asset to render verbatim):

    +------------------------------------------------+
    | C/P   / APP           SESSION 01   / LOCAL      |
    |------------------------------------------------|
    | CURRENT TASK                                   |
    |                                                |
    | [01] FIRST ROW                                 |
    | [02] SECOND ROW                                |
    | [03] THIRD ROW                                 |
    |                                                |
    |------------------------------------------------|
    | 3 OF 5 READY                  [ CONTINUE ]      |
    +------------------------------------------------+

A board game may retain its board as the dominant content surface instead of
forcing everything into rows. Chrome remains shared across application types.

- Use visible scroll affordances for long lists and prompt panels.
- If prompt and response list both scroll, give each bounded, accessible
  gesture zones and focus indicators.
- Never bury the only SUBMIT/CONTINUE action below a long scrolling list.
- Account for smartphone virtual keyboard and safe-area insets.
- Keep touch feedback deterministic. Browser keyboard focus must be visible
  even on inverted selected rows.
- Avoid decoration competing with data: whitespace is intentional.

## 4. Reusable component catalogue (names are proposals, not code APIs)

| Component | Contract |
| --- | --- |
| TerminalShell | Masthead + content + optional action footer |
| TerminalHeader | Shortmark, screen name, concise status |
| TerminalRule | Consistent 1px structural divider |
| TerminalRow | Numbered, full-width, tappable row |
| TerminalSelection | Inversion + visible select/order marker |
| TerminalAction | [ ACTION ], disabled/busy/error clearly stated |
| TerminalStatus | Text-first local/connected/waiting states |
| TerminalProgress | Fraction 3/5 and optional segmented marks |
| TerminalPrompt | Large multiline text with safe overflow |
| TerminalList | Vertical scroll, discoverable position |
| TerminalOverlay | Focused dialog, Back/Close, focus restored |
| TerminalQRCode | Valid QR plus proper quiet zone and plain URL |
| TerminalEmptyState | Useful reason + next action |
| TerminalError | ERROR: exact cause, safe recovery control |

**Interaction vocabulary**

- READY / BEREIT
- WAITING FOR 2 PLAYERS / WARTE AUF 2 SPIELER
- 3/5 RECEIVED / 3/5 ABGEGEBEN
- DISCONNECTED — RECONNECT / VERBINDUNG VERLOREN — VERBINDEN
- SELECT 2 DISTINCT CARDS / 2 UNTERSCHIEDLICHE KARTEN WÄHLEN
- ERROR: INVALID PACK / FEHLER: KARTENPAKET UNGÜLTIG

Use the selected UI locale consistently. Terminal formatting is *not*
permission to mix English and German user-facing text arbitrarily.

## 5. Rendering profiles

### Phone/browser: TERMINAL DARK

- Full #000 canvas, #fff text and outlines. Do not automatically inherit
  OS light/dark palettes; this visual profile is fixed.
- Genuine selectable text, semantic controls and keyboard focus. Do not
  emulate pixel fonts, CRT lines or boot screens.
- Responsive QA at 320px, 390px and desktop viewport widths.
- Scrollable lists, generous tap hit regions, accessible focus and action
  confirmation for destructive commands.
- Fixed/sticky footer where needed, always safe-area aware.
- Locally served assets; no third-party tracking/CDN/font dependencies.
- Animations are not part of core comprehension; reduced-motion supported.

Example starting CSS tokens:

    :root {
      color-scheme: dark;
      --terminal-bg: #000;
      --terminal-fg: #fff;
      --terminal-hairline: 1px;
      --terminal-focus: 2px;
      --terminal-unit: 4px;
      --terminal-font: ui-monospace, Menlo, "SFMono-Regular",
                       Consolas, "Liberation Mono", monospace;
    }

Do not copy these declarations into every page: later create one shared
small component stylesheet/bundle with regression tests.

### X4 native: TERMINAL PAPER

- 800×480, landscape, white paper and black text; a *stable* black masthead
  may use reverse typography to link visually to dark web clients.
- E-ink refresh is functional, not visual effect: partial refresh for small
  status changes, full refresh for legitimate page/round changes according to
  existing renderer policy.
- Avoid large *changing* filled black areas.
- Use FreeInkUI and Toybox interfaces; add a style adapter rather than
  forking every Activity's renderer or event loop.
- Validate touch map, glyph coverage, QR quiet zone, layout and refresh
  artifacts on physical X4 Pro.
- No layout that requires animation or high-frame-rate input.

### E-reader / EPUB boundaries

Recoloring app chrome must not rewrite upstream book content, pagination,
font settings, bookmarks or reading state. A text-first design *supports*
reading; it must not degrade it. Separate system chrome styles from embedded
book/document typography.

## 6. First pilot: Cards Against Whatever

Phone:
- Masthead: C/A/W / SESSION 01 / ROUND 02/10 / CZAR MAX.
- Large multiline prompt, numbered white-text answer rows.
- Selection inverts row to white background + black text; Pick 2/3 shows
  explicit ordering, not just a highlight.
- Answer list remains scrollable; selection count + SUBMIT always reachable.
- Judging displays anonymized shuffled answers; participant identities
  appear only after confirmed winner.
- Rando Cardrissian behaves like an anonymous contender until reveal.

X4:
- Shared game master: big prompt + round + czar + received count.
- Lobby QR and roster, then outcome and score.
- All updates event-driven, avoiding pointless E-Ink redraw.

Game requirements: [Cards Against Whatever](apps/cards-against-whatever.md).
The latest mockup is a **visual target**; the actual UI must also pass touch,
scrolling, localization and visibility tests.

## 7. Firmware-wide migration roadmap

**Stage A — Design pilot**
- This specification plus Cards Against Whatever UI, isolated feature branch.
- No global replacement, EPUB changes or OTA release.

**Stage B — Shared tokens and adapters**
- Expose semantic design tokens and Terminal components through existing
  src/apps_local/ui/ FreeInkUI/Toybox infrastructure.
- Add a tiny local browser stylesheet and shared behavior where warranted.
- Do not require every game to adopt it until tested.

**Stage C — Incremental application migration**
- First: Games shelf, launcher, generic dialogs.
- Next: utilities, settings, networking/status screens.
- Preserve board-centric apps' core presentation while unifying chrome.
- Finally: optionally reader chrome; book rendering is protected.

**Stage D — Optional global theme**
- Add a Theme switch only after stability is demonstrated.
- Preserve a fallback to the existing Toybox theme.
- Default migration only after whole-device regression acceptance.

Do **not** create one enormous formatting PR. Reuse components, land small
reviewable changes, and avoid breaks in upstream sync.

## 8. Acceptance and test gates

- Pixel/color audit: intended UI is only black and white.
- Smartphone screenshots: 320px, 390px, desktop; focus and selected states.
- X4 screenshots: 800×480; portrait only if separately implemented.
- Stress text: long German strings, umlauts, line breaks, Pick 3, scroll at
  maximum card length, browser text scaling and virtual keyboard.
- Touch: at least 44px targets, no inaccessible actions, keyboard fallback
  for browser, visible focus and full labels.
- Runtime: no new Internet requirement, stable memory and E-Ink redraw budget.
- Firmware build, simulator, host tests, integration UI smoke and *real X4*
  touch/WLAN/refresh validation.
- Never ship a theme-only mockup as an OTA candidate.

## 9. Relationship with existing documentation

- [Toybox design language](design-language.md) documents the implemented
  existing UI architecture and graphics principles.
- [CrossPlay identity](identity.md) documents the existing product voice.
- **This document** is the *proposed target* for a future redesign.
- [Cards Against Whatever](apps/cards-against-whatever.md) is the pilot app.

Open questions: compatible native monospace font sizes, RAM and flash costs,
the safest E-Ink inversion/refresh policy for shared headers, theme migration
compatibility and exact localization of short terminal labels.

**Guiding line: Less visual noise. More useful information.**
