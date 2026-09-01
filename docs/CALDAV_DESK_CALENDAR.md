# CalDAV Desk Calendar

## Goal

Turn the Xteink X4 Pro into a low-power E-Ink desk calendar while it is not being used as a reader.

The calendar is a sleep-screen feature, not a permanently running application. The rendered E-Ink image remains visible while the ESP32-S3 sleeps.

## MVP

The first implementation is intentionally read-only.

- CalDAV calendar source over HTTPS
- Basic authentication / app password
- Fetch upcoming events
- Cache events locally on the SD card
- Show today's date and upcoming appointments on the sleep screen
- Continue showing cached appointments when Wi-Fi or CalDAV is unavailable
- Manual sync
- Scheduled low-frequency sync
- No event creation, editing or deletion

## Desk calendar screen

Target layout for the 800x480 X4 Pro display:

```text
+--------------------------------------------------------------+
| TUESDAY                                      01 SEP 2026     |
|                                                              |
|                         09:42                                |
|                                                              |
| TODAY                                                        |
| 10:00  Daily                                                 |
| 13:30  Dentist                                               |
| 17:00  Pick up package                                       |
|                                                              |
| TOMORROW                                                     |
| 09:00  AWS planning                                          |
| 15:30  Appointment                                           |
|                                                              |
| CalDAV synced 08:05                              battery 78% |
+--------------------------------------------------------------+
```

The screen should prioritize readability from desk distance. Event descriptions and locations are secondary and may be truncated.

## E-Ink refresh strategy

Do not redraw the display every minute just to maintain a live clock.

Preferred behavior:

1. Render the desk calendar before entering deep sleep.
2. Keep the E-Ink panel unchanged while sleeping.
3. Wake only for useful changes:
   - configured CalDAV sync interval;
   - day boundary;
   - optional appointment boundary;
   - user interaction.
4. Fetch CalDAV only when Wi-Fi is required for a scheduled sync.
5. Render only when the visible calendar data changed.
6. Enter deep sleep again immediately after sync/render.

A clock displayed in desk-calendar mode therefore represents the most recent render time. A future option may allow periodic clock refreshes, but this must remain optional because it costs power and causes unnecessary E-Ink refreshes.

## Architecture

```text
CalDAV server
     |
     | HTTPS / WebDAV REPORT
     v
CalDavClient
     |
     v
IcsParser
     |
     v
CalendarStore ----> /calendar/cache.ics
     |
     v
DeskCalendarRenderer
     |
     v
SleepActivity
     |
     v
E-Ink display -> deep sleep
```

### Components

#### `CalDavClient`

Responsibilities:

- HTTPS requests
- authentication
- CalDAV discovery where practical
- `REPORT` request for a bounded time range
- response size limits
- timeout handling
- no writes in MVP

#### `IcsParser`

Only parse fields required by the device:

- `UID`
- `DTSTART`
- `DTEND`
- `SUMMARY`
- `LOCATION`
- `STATUS`
- `RRULE` for a deliberately limited supported subset

Unknown fields must be ignored safely.

#### `CalendarStore`

Keeps the last known-good event set on SD.

A failed sync must never delete a valid cache.

Suggested path:

```text
/.calendar/
  cache.ics
  sync-state.json
```

Credentials must not be written into the calendar cache.

#### `DeskCalendarRenderer`

Pure rendering layer. It receives date/time plus a bounded collection of normalized events and has no network responsibility.

This makes the UI testable in the simulator without a CalDAV server.

## CalDAV query window

For the first version, request only a bounded window:

- start: beginning of today
- end: 7 days in the future

The display itself should normally show:

- all remaining events today that fit;
- then upcoming events tomorrow / following days until the screen is full.

## Recurring events

Recurring events are one of the more complex parts of iCalendar.

MVP support should be conservative:

- normal one-off events: yes
- all-day events: yes
- simple daily/weekly recurrence: later in MVP if implementation remains small
- complex RRULE combinations: not initially
- server-expanded recurring instances: preferred when the CalDAV server provides them

An unsupported recurrence must not crash the parser.

## Time and RTC

Desk-calendar mode depends on reliable local time.

The X4 Pro port already has RTC hooks, but RTC hardware validation/integration must be completed before relying on autonomous timed wakeups.

Time sources, in priority order:

1. hardware RTC while sleeping;
2. NTP correction after Wi-Fi connection;
3. last known valid time as fallback.

Timezone handling must be explicit. CalDAV UTC timestamps must be converted to the configured local timezone before rendering.

## Settings

Planned settings:

```text
Desk calendar:       On / Off
CalDAV URL:          https://...
Username:            ...
Password/app token:  ...
Calendar:            ...
Sync interval:       1h / 3h / 6h / 12h / daily
Look ahead:          1 / 3 / 7 days
Show location:       On / Off
Show clock:          On / Off
```

The password should be hidden in the UI and preferably configured through the local web interface rather than entered with the reader buttons.

## Failure behavior

The calendar must remain useful offline.

| Failure | Behavior |
| --- | --- |
| No Wi-Fi | Render cached events |
| DNS/TLS failure | Keep cache, show stale sync indicator |
| Authentication rejected | Keep cache, show auth error in settings |
| Invalid ICS response | Reject new data, preserve last good cache |
| Empty valid calendar | Replace cache with empty calendar |
| RTC unavailable | Calendar mode remains accessible but scheduled wake/sync is disabled |

## Security

- HTTPS only by default.
- Do not log passwords, Authorization headers or full calendar payloads.
- Prefer app-specific passwords/tokens.
- Put strict response size and event-count limits in the embedded client.
- Treat all ICS text as untrusted input.
- Never allow CalDAV data to influence firmware paths or shell/system operations.

## X4 Pro safety

This feature stays entirely in the application layer.

It must not alter:

- bootloader;
- partition table;
- NVS partition layout;
- OTA metadata;
- X4 Pro flashing safeguards.

Calendar development must continue to follow `docs/X4PRO_FLASHING_GUIDE.md`.

## Implementation order

1. Calendar event model and renderer with fixture data.
2. Simulator screenshot/layout validation for 800x480.
3. ICS parser with unit fixtures.
4. SD cache and last-known-good semantics.
5. CalDAV read-only client.
6. Settings and local-web configuration.
7. Manual sync.
8. Scheduled RTC wake and low-power sync.
9. Hardware validation on X4 Pro.
10. Only then consider write support.

## Definition of done for MVP

- X4 Pro can be configured with a CalDAV account.
- Device downloads a bounded set of upcoming events.
- Events survive reboot/network loss through local caching.
- Entering sleep renders a readable desk-calendar view.
- Device can sleep with the calendar remaining visible.
- Scheduled refresh does not require the ESP32-S3 to stay awake.
- Failed sync cannot destroy the previous valid calendar cache.
- No CalDAV write methods are compiled into the MVP.
