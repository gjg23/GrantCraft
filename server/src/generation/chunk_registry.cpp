// server/src/generation/chunk_registry.cpp

#include "generation/chunk_registry.hpp"

#include <algorithm>
#include <chrono>

bool ChunkRegistry::has(const ChunkCoord& coord) const {
    return entries.count(coord) > 0;
}

void ChunkRegistry::request(const ChunkCoord& coord, EntityId subscriber, uint64_t nowUs) {
    bool isNew = (entries.find(coord) == entries.end());
    auto& entry = entries[coord];

    if (isNew) {
        entry.state = ChunkLifecycle::Requested;
        entry.requestedAtUs = nowUs;
    }

    if (subscriber == 0) return;  // spawn/no-recipient request

    const bool alreadyPending =
        std::find(entry.pendingRecipients.begin(),
                  entry.pendingRecipients.end(), subscriber)
            != entry.pendingRecipients.end();
    const bool alreadySent =
        std::find(entry.sentRecipients.begin(),
                  entry.sentRecipients.end(), subscriber)
            != entry.sentRecipients.end();

    if (alreadyPending || alreadySent) return;

    entry.pendingRecipients.push_back(subscriber);

    // get from world state
    if (entry.state == ChunkLifecycle::Sent)
        entry.state = ChunkLifecycle::WorldReady;
}

void ChunkRegistry::removeSubscriber(EntityId id, const ChunkCoord& coord) {
    auto it = entries.find(coord);
    if (it == entries.end()) return;
    auto& entry = it->second;

    auto pit = std::find(entry.pendingRecipients.begin(),
                         entry.pendingRecipients.end(), id);
    if (pit != entry.pendingRecipients.end())
        entry.pendingRecipients.erase(pit);

    auto sit = std::find(entry.sentRecipients.begin(),
                         entry.sentRecipients.end(), id);
    if (sit != entry.sentRecipients.end())
        entry.sentRecipients.erase(sit);
}

void ChunkRegistry::removeAllSubscriptions(EntityId id) {
    for (auto& [coord, entry] : entries) {
        auto pit = std::find(entry.pendingRecipients.begin(),
                             entry.pendingRecipients.end(), id);
        if (pit != entry.pendingRecipients.end())
            entry.pendingRecipients.erase(pit);

        auto sit = std::find(entry.sentRecipients.begin(),
                             entry.sentRecipients.end(), id);
        if (sit != entry.sentRecipients.end())
            entry.sentRecipients.erase(sit);
    }
}

void ChunkRegistry::markGenerating(const ChunkCoord& coord) {
    auto it = entries.find(coord);
    if (it != entries.end())
        it->second.state = ChunkLifecycle::Generating;
}

void ChunkRegistry::markWorldReady(const ChunkCoord& coord) {
    auto it = entries.find(coord);
    if (it != entries.end())
        it->second.state = ChunkLifecycle::WorldReady;
}

bool ChunkRegistry::markSentTo(const ChunkCoord& coord, EntityId id, uint64_t nowUs) {
    auto it = entries.find(coord);
    if (it == entries.end()) return false;
    auto& entry = it->second;

    auto pit = std::find(entry.pendingRecipients.begin(),
                         entry.pendingRecipients.end(), id);
    if (pit == entry.pendingRecipients.end()) return false;

    entry.pendingRecipients.erase(pit);
    entry.sentRecipients.push_back(id);

    if (entry.pendingRecipients.empty())
        entry.state = ChunkLifecycle::Sent;

    // Record latency sample
    if (entry.requestedAtUs > 0 && nowUs >= entry.requestedAtUs) {
        float latencyMs = static_cast<float>(nowUs - entry.requestedAtUs) / 1000.f;
        m_latencySamples.push_back(latencyMs);
        if (m_latencySamples.size() > MAX_LATENCY_SAMPLES)
            m_latencySamples.erase(m_latencySamples.begin());
    }

    return true;
}

std::vector<ChunkRegistry::SendWork> ChunkRegistry::collectSendWork() const {
    std::vector<SendWork> work;
    for (const auto& [coord, entry] : entries) {
        if (entry.state == ChunkLifecycle::WorldReady &&
            !entry.pendingRecipients.empty()) {
            work.push_back({coord, entry.pendingRecipients, entry.requestedAtUs});
        }
    }
    return work;
}

std::vector<ChunkCoord> ChunkRegistry::collectRequested() const {
    std::vector<ChunkCoord> result;
    for (const auto& [coord, entry] : entries) {
        if (entry.state == ChunkLifecycle::Requested)
            result.push_back(coord);
    }
    return result;
}

std::vector<RequestedChunkWithDist>
ChunkRegistry::collectRequestedByDistance(ChunkCoord center) const {
    std::vector<RequestedChunkWithDist> result;
    for (const auto& [coord, entry] : entries) {
        if (entry.state != ChunkLifecycle::Requested) continue;
        int dx = coord.x - center.x;
        int dy = coord.y - center.y;
        int dz = coord.z - center.z;
        result.push_back({coord, dx*dx + dy*dy + dz*dz});
    }
    std::sort(result.begin(), result.end(),
        [](const RequestedChunkWithDist& a, const RequestedChunkWithDist& b) {
            return a.distSq < b.distSq;
        });
    return result;
}

void ChunkRegistry::remove(const ChunkCoord& coord) {
    entries.erase(coord);
}

RegistryStats ChunkRegistry::getStats() const {
    RegistryStats s;
    for (const auto& [coord, entry] : entries) {
        switch (entry.state) {
            case ChunkLifecycle::Requested:  ++s.requested;  break;
            case ChunkLifecycle::Generating: ++s.generating; break;
            case ChunkLifecycle::WorldReady: ++s.worldReady; break;
            case ChunkLifecycle::Sent:       ++s.sent;       break;
        }
        s.totalPendingRecipients +=
            static_cast<uint32_t>(entry.pendingRecipients.size());
    }
    return s;
}