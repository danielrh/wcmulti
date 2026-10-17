#include <stdio.h>
#include "wcnet_entities.h"
#include "wcnet_log.h"

namespace wc {

EntityMap *g_entityMap = NULL;

NetworkShipId NetworkShipId::from_local(int slot) {
    if (slot < 0 || slot == kInvalidSlot || slot >= kCameraSlot) {
        return invalid();
    }
    if (!g_entityMap || is_temp_slot(slot)) {
        return NetworkShipId(slot);
    }
    return NetworkShipId(g_entityMap->local_to_net(slot));
}

int NetworkShipId::to_local() const {
    if (is_invalid()) {
        return kInvalidSlot;
    }
    if (!g_entityMap || is_temp_slot(id_)) {
        return id_;
    }
    return g_entityMap->net_to_local(id_);
}

bool EntityMap::is_mapped(int net) const {
    return net >= 0 && net < (int)netToLocal_.size() && netToLocal_[net] != -1;
}

bool EntityMap::is_local_mapped(int slot) const {
    for (size_t i = 0; i < netToLocal_.size(); i++) {
        if (netToLocal_[i] == slot) {
            return true;
        }
    }
    return false;
}

int EntityMap::net_to_local(int net) const {
    if (!is_mapped(net)) {
        wclog(2, "net id %d is not mapped to a local slot", net);
        return net;
    }
    return netToLocal_[net];
}

int EntityMap::local_to_net(int slot) const {
    for (size_t i = 0; i < netToLocal_.size(); i++) {
        if (netToLocal_[i] == slot) {
            return (int)i;
        }
    }
    wclog(2, "local slot %d has no network id", slot);
    return slot;
}

void EntityMap::record_spawn(int net, int slot) {
    if (!is_ship_slot(net) || net == kPlayerSlot) {
        wclog(2, "not recording mapping for temporary/player net id %d -> %d", net, slot);
        return;
    }
    while ((int)netToLocal_.size() <= net) {
        netToLocal_.push_back(-1);
    }
    if (net == ownShip_) {
        // We fly slot 0; the server's player (net 0) gets the body we just spawned.
        netToLocal_[net] = kPlayerSlot;
        netToLocal_[kPlayerSlot] = slot;
    } else {
        netToLocal_[net] = slot;
    }
}

void EntityMap::map(int net, int slot) {
    while ((int)netToLocal_.size() <= net) {
        netToLocal_.push_back(-1);
    }
    netToLocal_[net] = slot;
}

void EntityMap::unmap_local(int slot) {
    for (size_t i = 0; i < netToLocal_.size(); i++) {
        if (netToLocal_[i] == slot && (int)i != ownShip_ && (int)i != kPlayerSlot) {
            netToLocal_[i] = -1;
        }
    }
}

void EntityMap::record_despawn(int net) {
    if (net >= 0 && net < (int)netToLocal_.size() && net != ownShip_ && net != kPlayerSlot) {
        netToLocal_[net] = -1;
    }
}

void SpawnRegistry::add(const Spawn &spawn) {
    if (!spawn.has_ship_id()) {
        return;
    }
    if (spawns_.count(spawn.ship_id())) {
        wclog(2, "spawn registry already has ship %d", spawn.ship_id());
    }
    spawns_[spawn.ship_id()] = spawn;
}

void SpawnRegistry::remove(int net) {
    if (spawns_.erase(net) == 0) {
        wclog(3, "spawn registry has no ship %d to remove (missile or debris)", net);
    }
}

}  // namespace wc
