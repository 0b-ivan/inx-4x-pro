# Nextcloud calendar setup

The X4 Pro desk-calendar MVP uses **read-only CalDAV**. Nextcloud is the primary target.

## 1. Create a dedicated Nextcloud app password

Use a separate app password/token for the X4 Pro instead of storing your normal account password on the reader.

A useful name is:

```text
X4 Pro Calendar
```

Keep the token private. Do not commit it to this repository.

## 2. Copy the calendar's CalDAV address

Open Nextcloud Calendar and copy the CalDAV/private address for the calendar you want to display.

Use the **calendar collection URL**, not only the generic Nextcloud DAV root. A typical URL looks like:

```text
https://cloud.example.com/remote.php/dav/calendars/alice/personal/
```

The exact username/calendar slug depends on the Nextcloud installation, so copying the address from Nextcloud is safer than constructing it manually.

HTTPS is required by the MVP client.

## 3. Save Wi-Fi on the reader

Connect the reader to the desired Wi-Fi once through the normal Inx Wi-Fi screen. The calendar sync reuses that saved Wi-Fi credential; it does not duplicate the Wi-Fi password in the calendar config.

## 4. Create `calendar.json` on the SD card

For the MVP, put this file at the SD-card root:

```json
{
  "enabled": true,
  "calendarUrl": "https://cloud.example.com/remote.php/dav/calendars/alice/personal/",
  "username": "alice",
  "appPassword": "NEXTCLOUD-APP-PASSWORD",
  "wifiSsid": "HomeWiFi",
  "syncIntervalMinutes": 180,
  "lookAheadDays": 7
}
```

`wifiSsid` is optional. If omitted, the first saved Wi-Fi credential is used.

Supported values are intentionally bounded:

- `syncIntervalMinutes`: 30 to 1440 minutes
- `lookAheadDays`: 1 to 14 days

The config can alternatively live at:

```text
/.calendar/config.json
```

That path takes precedence over `/calendar.json`.

## 5. Enable the desk calendar

On the reader:

```text
Settings
  -> System settings
  -> Display
  -> Sleep Screen: Date Time

Settings
  -> System settings
  -> Clock
  -> Face: Desk Calendar
```

When the reader enters the date/time sleep screen, the desk-calendar renderer checks whether a sync is due.

If a sync is due it:

1. connects directly to the configured saved Wi-Fi;
2. sends a read-only CalDAV `REPORT` for the configured date window;
3. asks the server to expand recurring events in that window;
4. stores the validated result in `/.calendar/cache.ics`;
5. disconnects Wi-Fi again if the calendar code enabled it;
6. renders the cached appointments on the E-Ink screen.

If Wi-Fi, DNS, TLS, authentication, Nextcloud or parsing fails, the last known-good cache remains untouched.

## Current MVP limits

- read-only: no create/edit/delete operations;
- one configured calendar collection;
- no CalDAV account/calendar discovery UI yet;
- app password is currently stored as plain JSON on the SD card;
- recurring appointments rely on server-side CalDAV expansion;
- local-time handling uses the reader's configured timezone offset;
- autonomous timer wake is implemented but still needs validation on the physical X4 Pro together with the RTC/deep-sleep path.
