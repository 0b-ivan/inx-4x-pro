# Player system alpha

The shared player-system alpha exposes the active player directly in Battleship and provides the full profile under Apps > PLAYER.

Registered players are persisted on the X4 Pro SD card in `/crossplay/players.dat` using a small fixed-capacity binary store. The file is versioned, checksummed and written through a temporary file before rename. Up to eight local profiles are supported, including PIN credentials and per-game stats. Corrupt or future-version files are rejected instead of silently replaced. SQLite and the custom SQLite VFS are no longer required.

Guest progress stays in RAM until registration. Registering transfers the already-earned per-game progress into the persistent profile. PINs remain versioned PBKDF2-HMAC-SHA256 salt+hash credentials; ranks, level, class and play-style values stay derived rather than duplicated in storage.

Test focus: guest progression, registration, PIN login, binary persistence/reopen, integrity rejection, Battleship W/L/D, surrender-as-loss, rank, XP, level and profile radar.
