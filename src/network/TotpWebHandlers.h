#pragma once

class WebServer;

// Register Authenticator routes on the temporary File Transfer web server.
// This is intentionally not registered on the persistent Developer Mode surface.
void registerTotpWebRoutes(WebServer& server);
