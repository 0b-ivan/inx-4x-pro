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
