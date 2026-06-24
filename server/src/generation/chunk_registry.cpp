// server/src/generation/chunk_registry.cpp

#include "generation/chunk_registry.hpp"
#include <algorithm>

bool ChunkRegistry::has(const ChunkCoord& coord) const {
    return entries.count(coord) > 0;
}

void ChunkRegistry::request(const ChunkCoord& coord, EntityId subscriber) {
    auto& entry = entries[coord];

    // Avoid duplicate pendingRecipients
    auto& pend = entry.pendingRecipients;
    if (std::find(pend.begin(), pend.end(), subscriber) != pend.end())
        return;

    // skip if already sent to this subscriber
    auto& sent = entry.sentRecipients;
    if (std::find(sent.begin(), sent.end(), subscriber) != sent.end())
        return;

    pend.push_back(subscriber);
}

void ChunkRegistry::removeSubscriber(EntityId id, const ChunkCoord& coord) {
    auto it = entries.find(coord);
    if (it == entries.end()) return;

    auto& pend = it->second.pendingRecipients;
    pend.erase(std::remove(pend.begin(), pend.end(), id), pend.end());

    auto& sent = it->second.sentRecipients;
    sent.erase(std::remove(sent.begin(), sent.end(), id), sent.end());
}

void ChunkRegistry::removeAllSubscriptions(EntityId id) {
    for (auto& [coord, entry] : entries) {
        auto& pend = entry.pendingRecipients;
        pend.erase(std::remove(pend.begin(), pend.end(), id), pend.end());

        auto& sent = entry.sentRecipients;
        sent.erase(std::remove(sent.begin(), sent.end(), id), sent.end());
    }
}

void ChunkRegistry::markGenerating(const ChunkCoord& coord) {
    auto it = entries.find(coord);
    if (it == entries.end()) return;
    it->second.state = ChunkLifecycle::Generating;
}

void ChunkRegistry::markWorldReady(const ChunkCoord& coord) {
    auto it = entries.find(coord);
    if (it == entries.end()) return;
    it->second.state = ChunkLifecycle::WorldReady;
}

bool ChunkRegistry::markSentTo(const ChunkCoord& coord, EntityId id) {
    auto it = entries.find(coord);
    if (it == entries.end()) return false;

    auto& pend = it->second.pendingRecipients;
    auto  pit  = std::find(pend.begin(), pend.end(), id);
    if (pit == pend.end()) return false;

    pend.erase(pit);
    it->second.sentRecipients.push_back(id);

    // If no more pending recipients, advance to Sent
    if (pend.empty())
        it->second.state = ChunkLifecycle::Sent;

    return true;
}

std::vector<ChunkRegistry::SendWork> ChunkRegistry::collectSendWork() const {
    std::vector<SendWork> work;
    for (auto& [coord, entry] : entries) {
        if (entry.state == ChunkLifecycle::WorldReady &&
            !entry.pendingRecipients.empty()) {
            work.push_back({ coord, entry.pendingRecipients });
        }
    }
    return work;
}

std::vector<ChunkCoord> ChunkRegistry::collectRequested() const {
    std::vector<ChunkCoord> out;
    for (auto& [coord, entry] : entries) {
        if (entry.state == ChunkLifecycle::Requested) out.push_back(coord);
    }
    return out;
}

void ChunkRegistry::remove(const ChunkCoord& coord) {
    entries.erase(coord);
}

// stats
RegistryStats ChunkRegistry::getStats() const {
    RegistryStats s;
    for (auto& [coord, entry] : entries) {
        switch (entry.state) {
            case ChunkLifecycle::Requested:  ++s.requested;  break;
            case ChunkLifecycle::Generating: ++s.generating; break;
            case ChunkLifecycle::WorldReady: ++s.worldReady; break;
            case ChunkLifecycle::Sent:       ++s.sent;       break;
        }
        s.totalPendingRecipients += static_cast<uint32_t>(entry.pendingRecipients.size());
    }
    return s;
}