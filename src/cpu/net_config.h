#ifndef NET_CONFIG_H_
#define NET_CONFIG_H_

#include <stdint.h>

// How to find the other players, from the environment (the WCNET DOS
// command sets these variables before calling init_network):
//
//   TCP:       WCHOST unset = listen on WCPORT (server); set = connect to it.
//   lobbylink: WCROOM=<code> meets everyone in that room on the lobby
//              server WCLOBBY (default: the public test server).  The first
//              player in is the server.  WCPLAYERS sizes a room it makes and,
//              over TCP, says the room's size (default 2; 3 for seats made
//              for a third pilot),
//              WCLOBBY_ORIGIN overrides the Origin header ("" = none, for a
//              local server with --allow-no-origin), WCLOBBY_RELAY=1 forces
//              the TURN relay.
class NetConfig {
public:
    const char *host;
    const char *portstr;
    uint16_t port;

    const char *room;
    const char *lobby_url;
    const char *lobby_origin;
    bool lobby_relay;
    int lobby_players;

    NetConfig();
    void reset_from_env();
    void reset(const char *host, const char *portstr);
    bool use_lobby() const { return room != NULL && room[0] != 0; }

    static const char *kDefaultLobbyUrl;
    static const char *kDefaultPort;
};
extern NetConfig net_config;

#endif
