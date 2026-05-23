#pragma once

#include "search_common.hpp"

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

enum class ExactTranspositionBound : std::uint8_t {
    Exact,
    Lower,
    Upper,
};

struct ExactTranspositionEntry {
    ZobristHash hash = 0;
    int score = 0;
    std::int8_t empties = -1;
    std::int8_t best_move_index = -1;
    ExactTranspositionBound bound = ExactTranspositionBound::Exact;
    bool occupied = false;
};

class ExactTranspositionTable {
public:
    explicit ExactTranspositionTable(int root_empties) noexcept
        : entry_count_{entry_count_for_empties(root_empties)},
          entries_{entry_count_ == 0 ? nullptr
                                     : new (std::nothrow) ExactTranspositionEntry[entry_count_]} {}

    [[nodiscard]] std::optional<NodeResult> lookup(ZobristHash hash, int empties, int alpha,
                                                   int beta,
                                                   ExactEndgameStats& stats) const noexcept {
        if (entries_ == nullptr) {
            return std::nullopt;
        }

        ++stats.tt_lookups;
        const ExactTranspositionEntry& entry = entries_[entry_index(hash)];
        if (!entry.occupied || entry.hash != hash || entry.empties < empties) {
            return std::nullopt;
        }

        if (entry.bound == ExactTranspositionBound::Exact) {
            record_hit(stats, entry.bound);
            return node_result_from_entry(entry);
        }
        if (entry.bound == ExactTranspositionBound::Lower && entry.score >= beta) {
            record_hit(stats, entry.bound);
            return node_result_from_entry(entry);
        }
        if (entry.bound == ExactTranspositionBound::Upper && entry.score <= alpha) {
            record_hit(stats, entry.bound);
            return node_result_from_entry(entry);
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<Square> best_move_hint(ZobristHash hash,
                                                       int empties) const noexcept {
        if (entries_ == nullptr) {
            return std::nullopt;
        }

        const ExactTranspositionEntry& entry = entries_[entry_index(hash)];
        if (!entry.occupied || entry.hash != hash || entry.empties < empties) {
            return std::nullopt;
        }
        // Exact entries would have been returned by lookup(). Upper-bound moves were not strong
        // enough to raise alpha, so use only fail-high moves as first-move ordering hints.
        if (entry.bound != ExactTranspositionBound::Lower) {
            return std::nullopt;
        }

        return Square::from_index(static_cast<int>(entry.best_move_index));
    }

    void store(ZobristHash hash, int empties, int score, int original_alpha, int beta,
               const std::optional<Square>& best_move, ExactEndgameStats& stats) noexcept {
        if (entries_ == nullptr) {
            return;
        }

        ExactTranspositionEntry& entry = entries_[entry_index(hash)];
        if (entry.occupied && entry.hash != hash && empties < entry.empties) {
            ++stats.tt_rejected_stores;
            return;
        }

        const bool overwrites_entry = entry.occupied;
        const bool collides_with_different_hash = entry.occupied && entry.hash != hash;
        ExactTranspositionBound bound = ExactTranspositionBound::Exact;
        if (score <= original_alpha) {
            bound = ExactTranspositionBound::Upper;
        } else if (score >= beta) {
            bound = ExactTranspositionBound::Lower;
        }

        entry = ExactTranspositionEntry{
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

    std::size_t entry_count_ = 0;
    std::unique_ptr<ExactTranspositionEntry[]> entries_; // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                         // modernize-avoid-c-arrays)

    [[nodiscard]] std::size_t entry_index(ZobristHash hash) const noexcept {
        return static_cast<std::size_t>(hash) & (entry_count_ - 1);
    }

    [[nodiscard]] static NodeResult
    node_result_from_entry(const ExactTranspositionEntry& entry) noexcept {
        return node_result_from_transposition_entry(entry.score,
                                                    static_cast<int>(entry.best_move_index));
    }

    static void record_hit(ExactEndgameStats& stats, ExactTranspositionBound bound) noexcept {
        ++stats.tt_hits;
        switch (bound) {
        case ExactTranspositionBound::Exact:
            ++stats.tt_exact_hits;
            break;
        case ExactTranspositionBound::Lower:
            ++stats.tt_lower_hits;
            break;
        case ExactTranspositionBound::Upper:
            ++stats.tt_upper_hits;
            break;
        }
    }
};

struct ExactEndgameContext {
    explicit ExactEndgameContext(int root_empties) noexcept
        : use_tt_best_move_hints{root_empties >= 18}, transpositions{root_empties} {}

    ExactEndgameStats stats;
    // Smaller roots already have stable static ordering; hints are only worthwhile when TT reuse is
    // frequent enough to justify perturbing it.
    bool use_tt_best_move_hints = false;
    ExactTranspositionTable transpositions;
};

} // namespace othello::endgame_detail
