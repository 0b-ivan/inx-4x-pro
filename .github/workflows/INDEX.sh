#!/bin/bash
# This directory contains GitHub Actions workflows for CrossPlay
# 
# ✅ ACTIVE WORKFLOWS (3):
#   - crossplay-ci.yml        → Firmware builds + tests
#   - crossplay-release.yml   → Automated releases
#   - pr-formatting-check.yml → PR title validation
#
# ❌ DEPRECATED (Not here anymore):
#   - ci.yml              (removed: used master branch)
#   - release.yml         (removed: manual dispatch only)
#   - release_candidate.yml (removed: broken envs)
#   - bootstrap-ota-*     (removed: old system)
#   - game-boy-*          (removed: not for this fork)
#
# 📖 See README.md for detailed documentation

echo "CrossPlay Workflows"
ls -1 *.yml 2>/dev/null | sort
