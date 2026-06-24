#pragma once
// -------------------------------------------------
// server/ecs/registry.hpp
// ECS registry
// maps entity ID -> component maps
// -------------------------------------------------

#include <cstdint>
#include <unordered_map>
#include "ecs/components.hpp"

class Registry {
public:
    EntityId create() { return ++nextId; }

    void destroy(EntityId e) {
        positions.erase(e);
        networks.erase(e);
        interests.erase(e);
    }

    // Component access — returns nullptr if not present
    PositionComp*      position(EntityId e) { auto it = positions.find(e); return it!=positions.end() ? &it->second : nullptr; }
    NetworkComp*       network (EntityId e) { auto it = networks.find(e);  return it!=networks.end()  ? &it->second : nullptr; }
    ChunkInterestComp* interest(EntityId e) { auto it = interests.find(e); return it!=interests.end() ? &it->second : nullptr; }

    // Attach components
    PositionComp&      addPosition(EntityId e) { return positions[e]; }
    NetworkComp&       addNetwork (EntityId e) { return networks[e]; }
    ChunkInterestComp& addInterest(EntityId e) { return interests[e]; }

    // Iterate all entities that have a component
    auto& allPositions() { return positions; }
    auto& allNetworks()  { return networks; }

private:
    EntityId nextId = 0;
    std::unordered_map<EntityId, PositionComp>      positions;
    std::unordered_map<EntityId, NetworkComp>       networks;
    std::unordered_map<EntityId, ChunkInterestComp> interests;
};