# RSS configuration in the web interface

Open File Transfer on the reader, visit its displayed address, then **Settings → RSS feeds**.

- Enter a name, a direct RSS/Atom URL, and optional Basic-auth credentials.
- For Nextcloud use the login username and a device/app password from Personal settings → Security. With two-factor authentication the regular login password is rejected by Nextcloud clients.
- **Test connection** uses the current form without saving. It reports HTTP 401, 403, transport failure, invalid feed content, or success. It reads at most 256 KiB.
- Save, edit or delete up to eight feeds. An empty password field preserves the saved password; **Clear saved password** removes it.

The reader accepts RSS/Atom XML. Nextcloud News web pages and its JSON API are not supported by this RSS reader. SSO browser sessions are not used. A successful browser login therefore does not prove that a feed URL accepts Basic authentication.

API endpoints on the temporary File Transfer server: `GET /api/rss`, `POST /api/rss`, `POST /api/rss/test`, `POST /api/rss/delete`. They are not registered on the persistent Developer Mode server. POST bodies are JSON; edits/deletes use the list's numeric `index`. Passwords are never returned by the list endpoint. Settings reuse `/.crosspoint/rss.json` and the existing store's password obfuscation.

Verification: run `node host-tests/rss-web/test-form.cjs` for the dependency-free form tests. For browser tests run `NODE_PATH=/path/to/node_modules node host-tests/rss-web/test-ui.cjs` with Playwright and Chromium installed, then `pio run -e x4pro`. On the device save a feed, reopen File Transfer, check persistence, test a correct and incorrect app password, and open the saved feed in the RSS reader.

Nextcloud documentation: https://docs.nextcloud.com/server/latest/user_manual/en/session_management.html

## Incremental offline synchronization

**Sync cache** and **Sync all feeds** start a job and return immediately. The existing main loop advances it, fetching one feed or one article per step. The shared article fetcher still owns extraction and its AMP/Wikipedia fallbacks; a single article can therefore need multiple HTTP requests. A 250 ms interval between steps lets the web server handle status and cancellation requests.

The RSS section shows processed feeds, planned/saved articles, and feed/article failures. The planned total increases as each feed is read. Reloading Settings reconnects to the current job. **Cancel sync** stops before the next article; an in-flight connection or article fetch finishes or times out first. Leaving the page does not cancel the job. Feed edits, deletes, connection tests and schedule changes return HTTP 409 while synchronization runs.

- `POST /api/rss/sync` with `{ "index": null }` (all) or a numeric feed index: HTTP 202 plus progress JSON. An empty configuration or overlapping job returns 409.
- `GET /api/rss/sync`: progress JSON with `state`, `active`, `feedsTotal`, `feedsDone`, `feedsFailed`, `articlesTotal`, `articlesSaved`, `articlesFailed`, and `failedFeedMask`. Mask bits refer to positions in the job's feed snapshot (bit 0 for a single-feed job).
- `POST /api/rss/sync/cancel`: cancel and return the final progress snapshot. Repeated cancellation is harmless.

Terminal states are `complete`, `incomplete`, and `cancelled`; accepting the job is not completion. Only successful storage of every planned article with no feed failure produces `complete`. The scheduled completion day is recorded only then, and a failed settings write also makes the result incomplete. Timer wake uses the same steps, draining them before returning to sleep.

Pending markers are written before yielding for article downloads, including today's existing entries, so cancellation does not lose retries after a day change. The existing SD index and article format remain unchanged. Pending entries take priority when retaining the 80-entry cache; if pending entries exceed that capacity the feed is incomplete and the previous index is preserved, rather than silently dropping retries. A failed feed does not stop the remaining feeds.

Before merge, device validation remains required: Wi-Fi initially off/on, connection failure, missing SD card, save/reload/restart of web settings, timer wake, cancellation and retry, and a broken feed beside a working one. Browser tests use a mock API and do not establish device persistence. A stable release and merge remain gated on these device checks.
