# CrossPlay Workflows Overview

**This fork targets: X4 Pro + Sticky (ESP32-S3 devices only)**

## 🚀 Active Workflows (3 total)

| Workflow | File | Trigger | Status |
|----------|------|---------|--------|
| **CrossPlay CI** | `workflows/crossplay-ci.yml` | `push:main`, `pull_request` | ✅ |
| **CrossPlay Release** | `workflows/crossplay-release.yml` | `tag:v*` or `release: v*` commit | ✅ |
| **PR Formatting** | `workflows/pr-formatting-check.yml` | `pull_request_target` | ✅ |

## 📋 What Each Workflow Does

**CrossPlay CI** - Runs on every push to `main` and all PRs
- Builds firmware for x4pro and sticky
- Runs simulator
- Executes host test suites
- Validates stack usage

**CrossPlay Release** - Runs on version tags
- Builds release firmware
- Validates OTA images
- Publishes GitHub release with artifacts
- Creates `-full.bin` (USB install) + OTA `.bin`

**PR Formatting** - Validates PR titles
- Ensures semantic commit format: `feat:`, `fix:`, `chore:`, etc.

## ❌ Deprecated Workflows (Not in This Fork)

These **no longer exist** in this repo but may show up in GitHub Actions history:
- `bootstrap-ota-*` - Old bootstrap system (removed)
- `Game Boy X4 Pro Build` - Not for CrossPlay
- `Publish X4 Pro Bootstrap` - Deprecated

*GitHub keeps historical workflow runs even after files are deleted.*

## 🔍 How to Find Workflow Documentation

```
├── .github/
│   ├── WORKFLOWS.md ← You are here
│   └── workflows/
│       ├── README.md ← Detailed documentation
│       ├── crossplay-ci.yml
│       ├── crossplay-release.yml
│       └── pr-formatting-check.yml
```

**For detailed information:** See [workflows/README.md](workflows/README.md)

## 💡 Quick Reference

### Run local build (same as CI)
```bash
# Build x4pro
pio run -e x4pro

# Build sticky  
pio run -e sticky

# Run stack analysis (like CI does)
PLATFORMIO_BUILD_FLAGS="-fstack-usage -fcallgraph-info=su" pio run -e x4pro
python3 scripts_local/stack_budget.py --build-dir .pio/build/x4pro
```

### Create a release
```bash
# Update version in platformio.ini [crossplay] section
# Update release notes in crossplay-release.yml
git commit -m "release: v1.12.11"
git push origin main
# → Workflow runs automatically
```

---

*Last updated: 2026-09-03 | Cleanup: removed 3 deprecated workflows*
