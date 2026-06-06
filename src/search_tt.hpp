#pragma once

#include "search_common.hpp"
#include "search_runtime_options.hpp"
#include "tt_common.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <othello/hash.hpp>
#include <othello/square.hpp>
#include <span>

namespace othello::search_detail {

using tt_detail::bound_for_score;
using tt_detail::BoundKind;
using tt_detail::bucket_count_for_entry_count;
using tt_detail::proves_cutoff;

struct TranspositionEntry {
    ZobristHash hash = 0;
    std::uint64_t eval_identity = 0;
    std::uint32_t generation = 0;
    int depth = -1;
    int score = 0;
    int best_move_index = -1;
    SearchMode mode = SearchMode::FixedDepth;
    BoundKind bound = BoundKind::Exact;
    bool occupied = false;
};

struct TranspositionScope {
    SearchMode mode = SearchMode::FixedDepth;
    std::uint64_t eval_identity = 0;
};

struct TranspositionLookup {
    std::optional<NodeResult> cutoff;
    std::optional<Square> best_move_hint;
};

[[nodiscard]] constexpr std::uint32_t generation_age(std::uint32_t current_generation,
                                                     std::uint32_t entry_generation) noexcept {
    if (current_generation == 0 || entry_generation == 0) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    if (current_generation >= entry_generation) {
        return current_generation - entry_generation;
    }
    return current_generation + (std::numeric_limits<std::uint32_t>::max() - entry_generation);
}

[[nodiscard]] constexpr int replacement_protection_score(int depth,
                                                         std::uint32_t age) noexcept {
    constexpr int depth_weight = 4;
    constexpr std::uint32_t max_age_penalty = 8;
    const int normalized_depth = std::max(depth, 0);
    const int age_penalty = static_cast<int>(std::min(age, max_age_penalty));
    return normalized_depth * depth_weight - age_penalty;
}

[[nodiscard]] inline TranspositionEntry*
select_replacement_entry(std::span<TranspositionEntry> entries, ZobristHash hash,
                         TranspositionScope scope, std::uint32_t current_generation,
                         int incoming_depth) noexcept {
    TranspositionEntry* empty_slot = nullptr;
    TranspositionEntry* weakest_entry = nullptr;
    int weakest_score = 0;
    std::uint32_t weakest_age = 0;

    for (TranspositionEntry& entry : entries) {
        if (entry.occupied && entry.hash == hash && entry.mode == scope.mode &&
            entry.eval_identity == scope.eval_identity) {
            return &entry;
        }
        if (!entry.occupied) {
            if (empty_slot == nullptr) {
                empty_slot = &entry;
            }
            continue;
        }

        const std::uint32_t age = generation_age(current_generation, entry.generation);
        const int score = replacement_protection_score(entry.depth, age);
        if (weakest_entry == nullptr || score < weakest_score ||
            (score == weakest_score && age > weakest_age) ||
            (score == weakest_score && age == weakest_age && entry.depth < weakest_entry->depth)) {
            weakest_entry = &entry;
            weakest_score = score;
            weakest_age = age;
        }
    }

    if (empty_slot != nullptr) {
        return empty_slot;
    }

    const int incoming_score = replacement_protection_score(incoming_depth, 0);
    if (weakest_entry == nullptr || incoming_score < weakest_score) {
        return nullptr;
    }
    return weakest_entry;
}

class TranspositionTable {
public:
    explicit TranspositionTable(const SearchEngineOptions& options) noexcept
        : bucket_count_{normalized_bucket_count(options)},
          buckets_{bucket_count_ == 0 ? nullptr : new (std::nothrow) Bucket[bucket_count_]} {}

    [[nodiscard]] TranspositionLookup lookup(ZobristHash hash, TranspositionScope scope, int depth,
                                             int alpha, int beta, bool collect_best_move_hint,
                                             SearchStats& stats) const noexcept {
        if (buckets_ == nullptr) {
            return {};
        }

        ++stats.tt_lookups;
        const Bucket& bucket = buckets_[bucket_index(hash)];
        const TranspositionEntry* matching_entry = nullptr;
        for (const TranspositionEntry& entry : bucket.entries) {
            if (entry.occupied && entry.hash == hash && entry.mode == scope.mode &&
                entry.eval_identity == scope.eval_identity) {
                matching_entry = &entry;
                break;
            }
        }

        // The same depth guard is used for cutoff and ordering hints. A shallower
        // hint would be correctness-safe, but can perturb iterative search behavior.
        if (matching_entry == nullptr || matching_entry->depth < depth) {
            if (collect_best_move_hint) {
                ++stats.tt_move_ordering_probes;
            }
            return {};
        }

        const TranspositionEntry& entry = *matching_entry;
        if (proves_cutoff(entry.bound, entry.score, alpha, beta)) {
            record_hit(stats, entry.bound);
            return TranspositionLookup{.cutoff = node_result_from_entry(entry)};
        }

        if (!collect_best_move_hint) {
            return {};
        }

        ++stats.tt_move_ordering_probes;
        return TranspositionLookup{.best_move_hint = best_move_hint_from_entry(entry, stats)};
    }

    bool store(ZobristHash hash, TranspositionScope scope, std::uint32_t generation, int depth,
               int score, int original_alpha, int beta, const std::optional<Square>& best_move,
               SearchStats& stats) noexcept {
        if (buckets_ == nullptr) {
            return false;
        }

        TranspositionEntry* entry =
            replacement_entry(buckets_[bucket_index(hash)], hash, scope, generation, depth);
        if (entry == nullptr) {
            ++stats.tt_rejected_stores;
            return false;
        }

        const BoundKind bound = bound_for_score(score, original_alpha, beta);

        ++stats.tt_stores;
        if (entry->occupied) {
            ++stats.tt_overwrites;
            if (entry->hash != hash) {
                ++stats.tt_collisions;
            }
        }

        *entry = TranspositionEntry{
            .hash = hash,
            .eval_identity = scope.eval_identity,
            .generation = generation,
            .depth = depth,
            .score = score,
            .best_move_index = best_move.has_value() ? best_move->index() : -1,
            .mode = scope.mode,
            .bound = bound,
            .occupied = true,
        };
        return true;
    }

private:
    static constexpr std::size_t bucket_width = 4;
    static constexpr std::size_t default_entry_count = 1 << 18;

    struct Bucket {
        std::array<TranspositionEntry, bucket_width> entries{};
    };

    // SearchEngineOptions requests approximate entry count, not bucket count. Bucket indexing uses
    // a bit mask, so we round the requested bucket count up to a power of two. Very small positive
    // requests allocate one full bucket; oversized requests fall back to the default capacity.
    [[nodiscard]] static constexpr std::size_t
    normalized_bucket_count(const SearchEngineOptions& options) noexcept {
        if (!options.use_transposition_table || options.transposition_table_entries == 0) {
            return 0;
        }

        const std::size_t requested = options.transposition_table_entries;
        constexpr std::size_t max_power_of_two = std::size_t{1}
                                                 << (std::numeric_limits<std::size_t>::digits - 1);
        if (requested > max_power_of_two / bucket_width) {
            return bucket_count_for_entry_count(default_entry_count, bucket_width);
        }

        return bucket_count_for_entry_count(requested, bucket_width);
    }

    std::size_t bucket_count_ = 0;
    std::unique_ptr<Bucket[]> buckets_; // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                        // modernize-avoid-c-arrays)

    [[nodiscard]] std::size_t bucket_index(ZobristHash hash) const noexcept {
        return static_cast<std::size_t>(hash) & (bucket_count_ - 1);
    }

    [[nodiscard]] static TranspositionEntry*
    replacement_entry(Bucket& bucket, ZobristHash hash, TranspositionScope scope,
                      std::uint32_t generation, int depth) noexcept {
        return select_replacement_entry(std::span<TranspositionEntry>{bucket.entries}, hash, scope,
                                        generation, depth);
    }

    [[nodiscard]] static NodeResult
    node_result_from_entry(const TranspositionEntry& entry) noexcept {
        return node_result_from_transposition_entry(entry.score, entry.best_move_index);
    }

    [[nodiscard]] static std::optional<Square>
    best_move_hint_from_entry(const TranspositionEntry& entry, SearchStats& stats) noexcept {
        if (entry.best_move_index < Square::min_index ||
            entry.best_move_index > Square::max_index) {
            return std::nullopt;
        }

        std::optional<Square> best_move = Square::from_index(entry.best_move_index);
        if (!best_move.has_value()) {
            return std::nullopt;
        }

        ++stats.tt_move_ordering_hits;
        return best_move;
    }

    static void record_hit(SearchStats& stats, BoundKind bound) noexcept {
        ++stats.tt_hits;
        switch (bound) {
        case BoundKind::Exact:
            ++stats.tt_exact_hits;
            break;
        case BoundKind::Lower:
            ++stats.tt_lower_hits;
            break;
        case BoundKind::Upper:
            ++stats.tt_upper_hits;
            break;
        }
    }
};

} // namespace othello::search_detail
