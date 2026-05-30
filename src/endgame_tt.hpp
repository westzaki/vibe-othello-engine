#pragma once

#include "search_common.hpp"
#include "tt_common.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <othello/endgame.hpp>
#include <othello/hash.hpp>
#include <othello/square.hpp>

namespace othello::endgame_detail {

using search_detail::node_result_from_transposition_entry;
using search_detail::NodeResult;
using tt_detail::BoundKind;
using tt_detail::bound_for_score;
using tt_detail::proves_cutoff;

struct ExactTranspositionEntry {
    ZobristHash hash = 0;
    int score = 0;
    std::int8_t empties = -1;
    std::int8_t best_move_index = -1;
    BoundKind bound = BoundKind::Exact;
    bool occupied = false;
};

struct ExactTranspositionLookup {
    std::optional<NodeResult> cutoff;
    std::optional<Square> best_move_hint;
};

class ExactTranspositionTable {
public:
    explicit ExactTranspositionTable(int root_empties,
                                     std::optional<std::size_t> requested_entries) noexcept
        : bucket_count_{normalized_bucket_count(root_empties, requested_entries)},
          buckets_{bucket_count_ == 0 ? nullptr : new (std::nothrow) Bucket[bucket_count_]} {}

    [[nodiscard]] ExactTranspositionLookup lookup(ZobristHash hash, int empties, int alpha,
                                                  int beta, bool collect_best_move_hint,
                                                  ExactEndgameStats& stats) const noexcept {
        if (buckets_ == nullptr) {
            return {};
        }

        ++stats.tt_lookups;
        const Bucket& bucket = buckets_[bucket_index(hash)];
        const ExactTranspositionEntry* matching_entry = nullptr;
        for (const ExactTranspositionEntry& entry : bucket.entries) {
            if (entry.occupied && entry.hash == hash) {
                matching_entry = &entry;
                break;
            }
        }

        if (matching_entry == nullptr || matching_entry->empties < empties) {
            return {};
        }

        const ExactTranspositionEntry& entry = *matching_entry;
        if (proves_cutoff(entry.bound, entry.score, alpha, beta)) {
            record_hit(stats, entry.bound);
            return ExactTranspositionLookup{.cutoff = node_result_from_entry(entry)};
        }

        if (!collect_best_move_hint) {
            return {};
        }
        if (entry.bound != BoundKind::Lower) {
            return {};
        }

        ++stats.tt_move_ordering_probes;
        return ExactTranspositionLookup{
            .best_move_hint = best_move_hint_from_entry(entry, stats),
        };
    }

    void store(ZobristHash hash, int empties, int score, int original_alpha, int beta,
               const std::optional<Square>& best_move, ExactEndgameStats& stats) noexcept {
        if (buckets_ == nullptr) {
            return;
        }

        ExactTranspositionEntry* entry = replacement_entry(buckets_[bucket_index(hash)], hash,
                                                           empties);
        if (entry == nullptr) {
            ++stats.tt_rejected_stores;
            return;
        }

        const bool overwrites_entry = entry->occupied;
        const bool collides_with_different_hash = entry->occupied && entry->hash != hash;
        const BoundKind bound = bound_for_score(score, original_alpha, beta);

        *entry = ExactTranspositionEntry{
            .hash = hash,
            .score = score,
            .empties = static_cast<std::int8_t>(empties),
            .best_move_index =
                static_cast<std::int8_t>(best_move.has_value() ? best_move->index() : -1),
            .bound = bound,
            .occupied = true,
        };
        ++stats.tt_stores;
        if (overwrites_entry) {
            ++stats.tt_overwrites;
        }
        if (collides_with_different_hash) {
            ++stats.tt_collisions;
        }
    }

private:
    static constexpr std::size_t bucket_width = 4;
    static constexpr std::size_t max_diagnostic_entry_count = std::size_t{1} << 22;

    struct Bucket {
        std::array<ExactTranspositionEntry, bucket_width> entries{};
    };

    [[nodiscard]] static constexpr std::size_t entry_count_for_empties(int empties) noexcept {
        if (empties <= 8) {
            return 0;
        }
        if (empties <= 10) {
            return 1 << 14;
        }
        if (empties <= 12) {
            return 1 << 16;
        }
        return 1 << 20;
    }

    [[nodiscard]] static constexpr std::size_t
    normalized_bucket_count(int root_empties,
                            std::optional<std::size_t> requested_entries) noexcept {
        const std::size_t default_entry_count = entry_count_for_empties(root_empties);
        if (!requested_entries.has_value()) {
            return bucket_count_for_entry_count(default_entry_count);
        }
        if (*requested_entries == 0) {
            return 0;
        }
        if (*requested_entries > max_diagnostic_entry_count) {
            return bucket_count_for_entry_count(default_entry_count);
        }
        return bucket_count_for_entry_count(*requested_entries);
    }

    [[nodiscard]] static constexpr std::size_t
    bucket_count_for_entry_count(std::size_t entry_count) noexcept {
        if (entry_count == 0) {
            return 0;
        }
        const std::size_t requested_buckets = ((entry_count - 1) / bucket_width) + 1;
        return std::bit_ceil(requested_buckets);
    }

    std::size_t bucket_count_ = 0;
    std::unique_ptr<Bucket[]> buckets_; // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                        // modernize-avoid-c-arrays)

    [[nodiscard]] std::size_t bucket_index(ZobristHash hash) const noexcept {
        return static_cast<std::size_t>(hash) & (bucket_count_ - 1);
    }

    [[nodiscard]] static ExactTranspositionEntry*
    replacement_entry(Bucket& bucket, ZobristHash hash, int empties) noexcept {
        ExactTranspositionEntry* empty_slot = nullptr;
        ExactTranspositionEntry* shallowest = &bucket.entries.front();
        for (ExactTranspositionEntry& entry : bucket.entries) {
            if (entry.occupied && entry.hash == hash) {
                return &entry;
            }
            if (!entry.occupied) {
                if (empty_slot == nullptr) {
                    empty_slot = &entry;
                }
                continue;
            }
            if (entry.empties < shallowest->empties) {
                shallowest = &entry;
            }
        }

        if (empty_slot != nullptr) {
            return empty_slot;
        }

        if (empties < shallowest->empties) {
            return nullptr;
        }
        return shallowest;
    }

    [[nodiscard]] static NodeResult
    node_result_from_entry(const ExactTranspositionEntry& entry) noexcept {
        return node_result_from_transposition_entry(entry.score,
                                                    static_cast<int>(entry.best_move_index));
    }

    [[nodiscard]] static std::optional<Square>
    best_move_hint_from_entry(const ExactTranspositionEntry& entry,
                              ExactEndgameStats& stats) noexcept {
        if (entry.best_move_index < Square::min_index ||
            entry.best_move_index > Square::max_index) {
            return std::nullopt;
        }

        const std::optional<Square> best_move =
            Square::from_index(static_cast<int>(entry.best_move_index));
        if (!best_move.has_value()) {
            return std::nullopt;
        }

        ++stats.tt_move_ordering_hits;
        return best_move;
    }

    static void record_hit(ExactEndgameStats& stats, BoundKind bound) noexcept {
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

struct ExactEndgameContext {
    explicit ExactEndgameContext(int root_empties,
                                 const ExactEndgameOptions& options = {}) noexcept
        : root_empties{root_empties},
          transpositions{root_empties, options.transposition_table_entries} {}

    int root_empties = 0;
    ExactEndgameStats stats;
    ExactTranspositionTable transpositions;
};

} // namespace othello::endgame_detail
