#pragma once

namespace player {

// Registers the CrossPlay SQLite VFS backed by HalStorage and makes it the
// default VFS on real devices. The simulator keeps the host SQLite VFS.
bool registerPlayerSqliteVfs();

}  // namespace player
