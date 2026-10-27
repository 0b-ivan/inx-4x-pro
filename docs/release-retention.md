# GitHub Release retention – X4 Pro

The GitHub repository keeps stable versions and SD data forever. Firmware
prereleases generated on *experimental feature branches* are short-lived by
design; releasing a new build must not drown the Releases UI or break
download URLs needed by the device.

## Policy

| Release lane | Retained | Eligible for pruning? |
| --- | --- | --- |
| Stable \`vX.Y.Z\` (plus full-flash, ELF and firmware) | All | **Never** |
| \`trivia-study-packs\` public catalog and demo data | Always | **Never** |
| \`trivia-de-pack\`, \`trivia-de-preview-*\` translations | Always | **Never** |
| Milestones `v0.1.0-alpha.0`, `v1.0.20-x4pro-alpha.1/.8` | Permanent | **Never** |
| Eight explicitly named superseded Sep prototype alphas | Old intermediates only | **One-time cleanup**, >30 days old |
| \`X4 Pro Nightly Study\` | Latest **3** | Yes, only after new Nightly publishes and CI succeeds |
| \`X4 Pro Connect Four Nightly\` | Latest **2** | Yes, once older than protection window |

The optional `--prune-legacy-alpha` mode additionally removes **only**
`v1.0.20-x4pro-alpha.2` through `.7`, `v1.15.1-alpha` and
`v1.15.1-alpha.1` once each is older than 30 days, has the exact expected
name and firmware assets, and has not already been deleted. The initial
bootstrap release, the first and last v1.0.20 alpha, v1.15.1-alpha.2,
other alphas and **every stable version** are kept.

Only immutable feature releases matching both their exact release title and
\`vX.Y.Z-rc.YYYYMMDD.g<8 lowercase hex>\` tag, 40-digit source SHA prefix,
published prerelease status and both \`firmware.bin\` and
\`firmware.bin.sha256\` are eligible. Unknown names are never removed.
The cleanup run requires the newly published Study Nightly tag to exist,
to pass those validations and to remain protected. Versions younger than
**four hours** are never removed.

The script defaults to **dry run**. It only performs deletions with explicit
\`--apply\`; if any GitHub API call or guard fails, the run stops without
guessing. Apply deletes the obsolete GitHub release **and its tag** using
\`gh release delete --cleanup-tag\`. This invalidates direct URLs to those
specific old Nightly assets, which is why stable and data releases are excluded.

### Manually inspect a plan

With \`gh auth login\` completed, execute:

\`\`\`bash
python3 scripts_local/release_retention.py \
  --repository 0b-ivan/inx-4x-pro \
  --protect-tag v1.16.13-rc.20261027.ga3f90d4f \
  --keep-study 3 --keep-connect-four 2 --min-age-hours 4 \
  --prune-legacy-alpha
\`\`\`

Change \`--protect-tag\` to the **current public Study Nightly** when running
later. Deletion is normally performed by the guarded study Nightly release
workflow after **firmware**, **host tests**, **simulator** and the publish steps
succeed, with the exact new immutable tag.

Unit tests: \`python3 -m unittest discover -s host-tests/release -p 'test_release_retention.py' -v\`.

**Safety:** OTA consumers must use the stable release channel for regular
updates. Prerelease builds can become unavailable when superseded. Never
reference an experimental Nightly tag from a permanent README or SD catalog.
