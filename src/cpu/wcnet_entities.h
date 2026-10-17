/*
 *  Entity identity across machines.
 *
 *  The server's slot numbers are the network ids.  A client keeps a mapping
 *  from network id to its own slot, learned when it replays the server's
 *  spawn events.  The client's own ship is always its slot 0, and the slot the
 *  client spawns for "the server's ship N (our assigned id)" becomes the body
 *  of the server's player (network id 0); see EntityMap::record_spawn.
 *
 *  Temporary entities (bolts, explosions; slots >= 10) are never mapped: each
 *  machine simulates its own copies.
 */
#ifndef WCNET_ENTITIES_H_
#define WCNET_ENTITIES_H_

#include <map>
#include <vector>
#include "dosbox.h"
#include "wcnet_memory.h"
#include "wc.pb.h"

namespace wc {

class EntityMap;

// A ship identity.  Construct from a local slot or a network id; convert
// with to_local()/to_net().  The active EntityMap (if any) does the mapping;
// the server has none and uses the identity mapping.
class NetworkShipId {
public:
    static NetworkShipId invalid() { return NetworkShipId(-1); }
    static NetworkShipId from_net(int net) { return NetworkShipId(net); }
    static NetworkShipId from_local(int slot);
    static NetworkShipId from_top_level_local(int slot) { return from_local(top_level_parent(slot)); }

    bool is_invalid() const { return id_ < 0 || id_ >= kCameraSlot; }
    int to_net() const { return id_; }
    int to_local() const;

    bool operator==(const NetworkShipId &o) const { return id_ == o.id_; }
    bool operator!=(const NetworkShipId &o) const { return id_ != o.id_; }
    bool operator<(const NetworkShipId &o) const { return id_ < o.id_; }

private:
    explicit NetworkShipId(int id) : id_(id) {}
    int id_;
};

// net <-> local slot mapping used by clients.
class EntityMap {
public:
    EntityMap() : ownShip_(0) {}

    void reset() { netToLocal_.clear(); }
    void set_own_ship(int netId) { ownShip_ = netId; }
    int own_ship() const { return ownShip_; }

    bool is_mapped(int net) const;
    bool is_local_mapped(int slot) const;  // some network id maps to this slot
    int net_to_local(int net) const;   // returns net when unmapped (logged)
    int local_to_net(int slot) const;  // returns slot when unmapped (logged)

    // Called after replaying a spawn/fire that produced `slot` for network
    // id `net`.  Applies the player-slot swap described in the file comment.
    void record_spawn(int net, int slot);
    void unmap_local(int slot);        // forget every net id that maps to this local slot (not the players')
    // net is in slot, whatever it is (a drone's copy of the server's player).
    void map(int net, int slot);
    void record_despawn(int net);

private:
    std::vector<int> netToLocal_;
    int ownShip_;
};

// Installed while a client session is active; NULL on the server.
extern EntityMap *g_entityMap;

// Ships currently alive as the server knows them, used to bring late joiners
// up to date.
class SpawnRegistry {
public:
    void add(const Spawn &spawn);
    void remove(int net);
    void clear() { spawns_.clear(); }
    const std::map<int, Spawn> &all() const { return spawns_; }

private:
    std::map<int, Spawn> spawns_;
};

}  // namespace wc

#endif
