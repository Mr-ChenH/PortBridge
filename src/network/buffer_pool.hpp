#pragma once
#include "portbridge/types.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace portbridge::network_detail {
// A leased buffer is reusable only after every consumer releases it. Retained
// payloads own their bytes, but must not keep an old engine's free cache alive.
struct BufferPool : std::enable_shared_from_this<BufferPool> {
    std::mutex mutex;
    static constexpr std::array<std::size_t, 5> sizes{{0, 256, 2048, 8192, 65536}};
    std::array<std::vector<std::unique_ptr<Bytes>>, sizes.size()> free;

    std::shared_ptr<Bytes> acquire(std::size_t requested = sizes.back()) {
        if (requested > sizes.back()) throw std::length_error("Receive buffer exceeds pool block size");
        const auto index = static_cast<std::size_t>(std::lower_bound(sizes.begin(), sizes.end(), requested) - sizes.begin());
        std::unique_ptr<Bytes> bytes;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!free[index].empty()) { bytes = std::move(free[index].back()); free[index].pop_back(); }
        }
        if (!bytes) { bytes = std::make_unique<Bytes>(); bytes->reserve(sizes[index]); }
        bytes->resize(requested);
        return {bytes.release(), [weakPool = std::weak_ptr<BufferPool>(shared_from_this()), index](Bytes* p) {
            std::unique_ptr<Bytes> owned(p);
            const auto pool = weakPool.lock();
            if (!pool) return;
            std::lock_guard<std::mutex> lock(pool->mutex);
            if (pool->free[index].size() < 32) pool->free[index].push_back(std::move(owned));
        }};
    }
};
} // namespace portbridge::network_detail
