#!/usr/bin/env bash
# CrossPlay GitHub repository maintenance. Requires authenticated gh CLI.
# No mutation by default. An owner must explicitly invoke an apply mode.
set -euo pipefail
REPO="0b-ivan/inx-4x-pro"
MODE="--dry-run"
if [[ $# -gt 0 ]]; then MODE="$1"; fi
case "$MODE" in
  --dry-run|--apply-branches|--protect-main) ;;
  *) echo "Usage: bash $0 [--dry-run | --apply-branches | --protect-main]" >&2; exit 2 ;;
esac
command -v gh >/dev/null || { echo "Install/authenticate GitHub CLI (gh)." >&2; exit 1; }
gh api "repos/$REPO" --jq '.full_name' >/dev/null
MAIN_SHA="$(gh api "repos/$REPO/branches/main" --jq '.commit.sha')"
echo "Repository: $REPO | main: $MAIN_SHA | mode: $MODE"

if [[ "$MODE" == "--protect-main" ]]; then
  # Require the verified CI repair to have merged and main to be green.
  merged="$(gh pr view 40 --repo "$REPO" --json mergedAt --jq '.mergedAt != null')"
  [[ "$merged" == "true" ]] || {
    echo "Refusing: merge the green CI/nightly repair PR #40 first." >&2
    exit 1
  }
  conclusion="$(gh run list --repo "$REPO" --workflow crossplay-ci.yml --branch main --limit 1 --json conclusion --jq '.[0].conclusion')"
  [[ "$conclusion" == "success" ]] || {
    echo "Refusing: latest main CrossPlay CI not successful ($conclusion)." >&2
    exit 1
  }
  if gh api "repos/$REPO/branches/main/protection" >/dev/null 2>&1; then
    echo "main already has protection; inspect and amend, do not overwrite."
    exit 0
  fi
  gh api --method PUT "repos/$REPO/branches/main/protection" \
    --header "Accept: application/vnd.github+json" \
    --input - <<'JSON'
{
  "required_status_checks": {
    "strict": true,
    "contexts": ["firmware (x4pro)", "simulator (x4 pro)", "host tests"]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": {
    "dismiss_stale_reviews": false,
    "require_code_owner_reviews": false,
    "required_approving_review_count": 0
  },
  "restrictions": null,
  "required_linear_history": false,
  "allow_force_pushes": false,
  "allow_deletions": false
}
JSON
  echo "main protection applied: PRs + three mandatory CI checks."
  exit 0
fi

# This static candidate list had zero unique commits ahead of main on
# 2026-10-26. Every candidate is re-verified when the command executes.
while IFS= read -r branch; do
  [[ -n "$branch" ]] || continue
  case "$branch" in
    main|feature/usb-passkey|fix/i18n-translation-quality-audit|debug/*)
      echo "SKIP protected active branch: $branch"; continue ;;
  esac
  if ! branch_sha="$(gh api "repos/$REPO/branches/$branch" --jq '.commit.sha' 2>/dev/null)"; then
    echo "SKIP missing: $branch"; continue
  fi
  open_prs="$(gh api "repos/$REPO/pulls?state=open&per_page=100" \
    --jq "map(select(.head.ref == \"$branch\")) | length")"
  if [[ "$open_prs" != 0 ]]; then
    echo "SKIP $branch: $open_prs open PR(s)"; continue
  fi
  ahead="$(gh api "repos/$REPO/compare/main...$branch" --jq '.ahead_by')"
  if [[ "$ahead" != 0 ]]; then
    echo "SKIP $branch: $ahead unique commit(s) ahead of current main"; continue
  fi
  if [[ "$MODE" != "--apply-branches" ]]; then
    echo "DRY-RUN candidate: $branch ($branch_sha; ahead=0; open PRs=0)"
    continue
  fi
  # GitHub deletes refs without compare-and-swap. Recheck just beforehand.
  current_sha="$(gh api "repos/$REPO/branches/$branch" --jq '.commit.sha')"
  [[ "$current_sha" == "$branch_sha" ]] || {
    echo "SKIP $branch: head changed during audit"; continue
  }
  gh api --method DELETE "repos/$REPO/git/refs/heads/$branch" >/dev/null
  echo "DELETED: $branch"
done <<'BRANCHES'
automation/player-visibility
automation/player-visibility-2
automation/player-visibility-3
copilot/fix-firmware-x4pro-job-failure
copilot/fix-github-actions-job-another-one
docs/varagh-migration-plan
feature/battleship-browser-input-lock
feature/player-system
feature/totp-web-ui
feature/touch-input-biscuit-apps
fix/authenticator-i18n
fix/phase0-tarot-back-i18n
fix/tarot-nav-wifi-wake
fix/v1.12.26-release-notes
integration/1.15.1
release/v1.12.24-authenticator-complete
BRANCHES

if [[ "$MODE" == "--dry-run" ]]; then
  echo "No deletion performed. Use --apply-branches only after reviewing output."
fi
