#pragma once
// -------------------------------------------------
// server/ecs/registry.hpp
// ECS registry: maps entity ID -> component maps
// -------------------------------------------------

#include <cstdint>
#include <unordered_map>
#include <vector>
#include "ecs/components.hpp"

class Registry {
public:
    EntityId create() { return ++nextId; }

    void destroy(EntityId e) {
        positions.erase(e);
        networks.erase(e);
        interests.erase(e);
    }

    // Component access
    PositionComp*       position(EntityId e)       { auto it = positions.find(e); return it!=positions.end() ? &it->second : nullptr; }
    const PositionComp* position(EntityId e) const { auto it = positions.find(e); return it!=positions.end() ? &it->second : nullptr; }
    NetworkComp*        network (EntityId e)       { auto it = networks.find(e);  return it!=networks.end()  ? &it->second : nullptr; }
    const NetworkComp*  network (EntityId e) const { auto it = networks.find(e);  return it!=networks.end()  ? &it->second : nullptr; }
    ChunkInterestComp*  interest(EntityId e)       { auto it = interests.find(e); return it!=interests.end() ? &it->second : nullptr; }

    // Attach components
    PositionComp&      addPosition(EntityId e) { return positions[e]; }
    NetworkComp&       addNetwork (EntityId e) { return networks[e]; }
    ChunkInterestComp& addInterest(EntityId e) { return interests[e]; }

    // Iterate all entities that have a component
    auto&       allPositions()       { return positions; }
    const auto& allPositions() const { return positions; }
    auto&       allNetworks()        { return networks; }
    const auto& allNetworks()  const { return networks; }

    // collect entity IDs with positions
    std::vector<EntityId> allPositionIds() const {
        std::vector<EntityId> ids;
        ids.reserve(positions.size());
        for (const auto& [id, _] : positions)
            ids.push_back(id);
        return ids;
    }

    // pre-reserve to avoid rehashing during parallel access
    void reservePositions(size_t n) { positions.reserve(n); }

private:
    EntityId nextId = 0;
    std::unordered_map<EntityId, PositionComp>      positions;
    std::unordered_map<EntityId, NetworkComp>       networks;
    std::unordered_map<EntityId, ChunkInterestComp> interests;
};