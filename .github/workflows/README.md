# GitHub Actions Workflows

**Fork-Status:** CrossPlay (X4 Pro + Sticky) only. Upstream workflows (Game Boy, Bootstrap OTA) are not maintained in this fork.

## ✅ Active Workflows

Only **3 workflows** are active in this fork:

### 1. CrossPlay (`crossplay-ci.yml`)
```yaml
Trigger: push:main, pull_request, workflow_dispatch
Purpose: Firmware builds + tests
Jobs: firmware (x4pro, sticky), simulator, host-tests
Status: ✓ ACTIVE
```

### 2. CrossPlay Release (`crossplay-release.yml`)
```yaml
Trigger: tag:v* OR commit:release: v*
Purpose: Automated release publishing
Jobs: build, validate, publish
Status: ✓ ACTIVE
```

### 3. PR Formatting (`pr-formatting-check.yml`)
```yaml
Trigger: pull_request_target
Purpose: PR title validation (Semantic Versioning)
Jobs: title-check
Status: ✓ ACTIVE
```

## ⚠️ Historical Workflows (Deprecated, Not in This Fork)

These appear in GitHub Actions → All workflows history but **do not run**:
- ❌ `bootstrap-ota-*` - Old bootstrap releases
- ❌ `Game Boy X4 Pro Build` - Not for CrossPlay fork
- ❌ `Publish X4 Pro Bootstrap` - Deprecated

**Why they appear:** GitHub keeps historical workflow runs after files are deleted.

## Übersicht

Dieses Repository nutzt GitHub Actions für CI/CD mit folgenden Workflows:

| Workflow | Trigger | Zweck |
|----------|---------|-------|
| **CrossPlay** (`crossplay-ci.yml`) | `push:main`, `pull_request`, `workflow_dispatch` | Firmware-Builds und Tests für x4pro/sticky |
| **CrossPlay Release** (`crossplay-release.yml`) | Tag `v*` oder Commit `release: v*` auf main | Vollautomatische Release-Publishing |
| **PR Formatting** (`pr-formatting-check.yml`) | `pull_request_target` | PR-Titel Validierung (Semantic Versioning) |

## Workflows im Detail

### CrossPlay CI (`crossplay-ci.yml`)

**Trigger:**
- Push zu `main` branch
- Alle Pull Requests
- Manuell via `workflow_dispatch`

**Jobs:**
1. **firmware** - Baut für `x4pro` und `sticky`, mit Stack-Analyse
   - Analysiert Stack-Usage pro Funktion
   - Validiert dass Stack in zugewiesener Task-Size passt
2. **simulator** - Baut den Simulator für x4pro
3. **host-tests** - Führt alle Host-Tests durch

**Besonderheiten:**
- Nutzt Clean Build für Stack-Analyse (Stack-Daten nur bei vollständigem Build vorhanden)
- Parallele Job-Ausführung für Geschwindigkeit
- `submodules: false` da `freeink-sdk` vendored ist

### CrossPlay Release (`crossplay-release.yml`)

**Trigger:**
- Git Tag: `v*` (z.B. `v1.12.10`)
- Commit auf main mit Message genau `release: v*`

**Prozess:**
1. Release-Identity validieren
2. Host-Test Suite durchlaufen (`host-tests/release/run.sh`)
3. Firmware für x4pro und sticky bauen
4. Bootloader + Partition Table + App mergen → `-full.bin`
5. OTA-Images validieren (Magic Byte, Chip-ID, Board-Tag, Version)
6. Merged Images validieren (richtige Bytes an richtigen Offsets)
7. GitHub Release erstellen und Artefakte hochladen

**Artefakte pro Board:**
- `crossplay-vX.Y.Z-<board>-full.bin` - Kompletter USB-Flash
- `firmware.bin` (x4pro) / `firmware-sticky.bin` - OTA-Image
- `crossplay-vX.Y.Z-<board>.elf` - Debug-Symbole

### PR Formatting (`pr-formatting-check.yml`)

**Trigger:** Pull Request opened/reopened/edited

**Validierung:**
- PR-Titel folgt Conventional Commits (z.B. `feat: ...`, `fix: ...`)
- Regex: `(feat|fix|refactor|docs|chore|perf)(\(.+\))?!?: .+`

## Release durchführen

### Option 1: Commit-basiert (empfohlen)

```bash
# 1. Version in platformio.ini [crossplay] section aktualisieren
# [crossplay]
# version = 1.12.11

# 2. Release-Notes in crossplay-release.yml aktualisieren (body: |...)

# 3. Commit mit exakter Message
git add platformio.ini .github/workflows/crossplay-release.yml
git commit -m "release: v1.12.11"

# 4. Push zu main
git push origin main
# → Workflow startet automatisch
```

### Option 2: Tag-basiert

```bash
# 1. Tag erstellen
git tag -a v1.12.11 -m "CrossPlay v1.12.11"

# 2. Push
git push origin v1.12.11
# → Workflow startet automatisch
```

## Wartung

### Abhängigkeiten aktualisieren

**PlatformIO Core:** Aktualisiere die URL in Workflows:
```yaml
uv pip install --system -U https://github.com/pioarduino/platformio-core/archive/refs/tags/vX.Y.Z.zip
```

**Python Version:** Aktualisiere in allen Workflows:
```yaml
python-version: '3.14'
```

### Neue Environments hinzufügen

1. Umgebung in `platformio.ini` definieren: `[env:my_board]`
2. Release-Matrix in `crossplay-release.yml` erweitern:
```yaml
matrix:
  include:
    - pio_env: gh_release_my_board
      asset: firmware-my_board.bin
```
3. OTA-Validierung anpassen (Board-Tag, Naming)

### CI/CD optimieren

- **Build-Cache:** `~/.platformio` wird zwischen Runs gecacht
- **Submodules:** `false` nutzen da freeink-sdk vendored
- **Python:** `3.13+` für bessere Performance

## Häufige Fehler

| Fehler | Ursache | Lösung |
|--------|--------|--------|
| OTA-Validierung schlägt fehl | Version nicht in Binary | `CROSSPOINT_VERSION` in build_flags korrekt setzen |
| Stack überschritten | Neue große lokale Variable | Stack-Size in `xTaskCreate()` erhöhen |
| Publish schlägt fehl | GitHub Token ungültig | Token-Permissions in Repo überprüfen |
| Tag-Release konnt nicht gefunden werden | fetch-depth: 0 nötig | Workspace checkout mit `fetch-depth: 0` |

## Siehe auch

- [CLAUDE.md](../CLAUDE.md) - AI Agent Guidelines
- [AGENTS.md](../AGENTS.md) - Development Guide
- `platformio.ini` - Build-Konfiguration
- `host-tests/release/run.sh` - Release-Validierungen
