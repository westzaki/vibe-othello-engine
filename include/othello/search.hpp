#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <othello/board.hpp>
#include <othello/evaluation.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello::search_detail {
struct SearchSessionState;
}

namespace othello {

struct SearchStats {
    std::uint64_t nodes = 0;
    // Depth-limited search observability. game_over_nodes counts terminal
    // no-move/no-pass nodes reached by search, not an eager probe on every node.
    std::uint64_t beta_cutoffs = 0;
    std::uint64_t beta_cutoffs_first_move = 0;
    std::uint64_t searched_moves = 0;
    std::uint64_t legal_move_nodes = 0;
    std::uint64_t eval_calls = 0;
    std::uint64_t pass_nodes = 0;
    std::uint64_t game_over_nodes = 0;

    std::uint64_t tt_lookups = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_exact_hits = 0;
    std::uint64_t tt_lower_hits = 0;
    std::uint64_t tt_upper_hits = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t tt_leaf_stores = 0;
    std::uint64_t tt_overwrites = 0;
    std::uint64_t tt_collisions = 0;
    std::uint64_t tt_rejected_stores = 0;
    std::uint64_t tt_move_ordering_probes = 0;
    std::uint64_t tt_move_ordering_hits = 0;
    std::uint64_t tt_move_ordering_used = 0;

    std::uint64_t pvs_scouts = 0;
    std::uint64_t pvs_researches = 0;
    std::uint64_t pvs_scout_cutoffs = 0;

    std::uint64_t aspiration_searches = 0;
    std::uint64_t aspiration_researches = 0;
    std::uint64_t aspiration_fail_lows = 0;
    std::uint64_t aspiration_fail_highs = 0;
    std::uint64_t aspiration_full_window_fallbacks = 0;
    std::uint64_t aspiration_fail_low_distance_sum = 0;
    std::uint64_t aspiration_fail_high_distance_sum = 0;
    // Cumulative record distances for failed aspiration searches. Aggregating
    // stats keeps the maximum; a delta reports the new cumulative maximum only
    // when the interval raised the previous record.
    std::uint64_t aspiration_fail_low_distance_max = 0;
    std::uint64_t aspiration_fail_high_distance_max = 0;

    std::uint64_t dynamic_ordering_nodes = 0;
    std::uint64_t dynamic_ordering_moves = 0;
};

namespace search_stats_detail {

inline constexpr auto additive_members = std::to_array<std::uint64_t SearchStats::*>({
    &SearchStats::nodes,
    &SearchStats::beta_cutoffs,
    &SearchStats::beta_cutoffs_first_move,
    &SearchStats::searched_moves,
    &SearchStats::legal_move_nodes,
    &SearchStats::eval_calls,
    &SearchStats::pass_nodes,
    &SearchStats::game_over_nodes,
    &SearchStats::tt_lookups,
    &SearchStats::tt_hits,
    &SearchStats::tt_exact_hits,
    &SearchStats::tt_lower_hits,
    &SearchStats::tt_upper_hits,
    &SearchStats::tt_stores,
    &SearchStats::tt_leaf_stores,
    &SearchStats::tt_overwrites,
    &SearchStats::tt_collisions,
    &SearchStats::tt_rejected_stores,
    &SearchStats::tt_move_ordering_probes,
    &SearchStats::tt_move_ordering_hits,
    &SearchStats::tt_move_ordering_used,
    &SearchStats::pvs_scouts,
    &SearchStats::pvs_researches,
    &SearchStats::pvs_scout_cutoffs,
    &SearchStats::aspiration_searches,
    &SearchStats::aspiration_researches,
    &SearchStats::aspiration_fail_lows,
    &SearchStats::aspiration_fail_highs,
    &SearchStats::aspiration_full_window_fallbacks,
    &SearchStats::aspiration_fail_low_distance_sum,
    &SearchStats::aspiration_fail_high_distance_sum,
    &SearchStats::dynamic_ordering_nodes,
    &SearchStats::dynamic_ordering_moves,
});

inline constexpr auto max_members = std::to_array<std::uint64_t SearchStats::*>({
    &SearchStats::aspiration_fail_low_distance_max,
    &SearchStats::aspiration_fail_high_distance_max,
});

inline constexpr std::size_t tracked_member_count = additive_members.size() + max_members.size();

} // namespace search_stats_detail

inline void accumulate_search_stats(SearchStats& total, const SearchStats& stats) noexcept {
    for (const auto member : search_stats_detail::additive_members) {
        total.*member += stats.*member;
    }
    for (const auto member : search_stats_detail::max_members) {
        total.*member = std::max(total.*member, stats.*member);
    }
}

[[nodiscard]] inline SearchStats search_stats_delta(const SearchStats& after,
                                                    const SearchStats& before) noexcept {
    SearchStats delta;
    for (const auto member : search_stats_detail::additive_members) {
        delta.*member = after.*member - before.*member;
    }
    for (const auto member : search_stats_detail::max_members) {
        delta.*member = after.*member > before.*member ? after.*member : 0;
    }
    return delta;
}

enum class ExactEndgameRootPolicy {
    FixedThreshold,
    Adaptive16,
};

enum class AspirationProfile {
    Fixed,
    ScoreDeltaAware,
};

enum class ExactEndgameRootSkipReason {
    None,
    Disabled,
    AboveThreshold,
    AdaptiveRootPass,
    AdaptiveTooManyLegalMoves,
    AdaptiveOpponentTooManyLegalMoves,
};

struct ExactEndgameRootDecision {
    bool solve_exact = false;
    int empty_count = 0;
    int legal_moves_current = 0;
    int legal_moves_opponent = 0;
    ExactEndgameRootSkipReason skip_reason = ExactEndgameRootSkipReason::None;
};

enum class SearchScoreKind {
    Heuristic,
    ExactDiscMarginScaled,
};

enum class SearchMode {
    FixedDepth,
    Iterative,
    ExactEndgame,
    Selective,
};

struct SearchResult {
    std::optional<Square> best_move;
    // Depth-limited searches report a heuristic score from the search/evaluator.
    // If exact endgame solving is used at the root, this is the exact final disc
    // margin converted onto the search score scale.
    int score = 0;
    // Requested/effective depth for depth-limited searches. Exact root endgame
    // results report the input board's empty count instead.
    int depth = 0;
    std::uint64_t nodes = 0;
    std::vector<Square> principal_variation;
    SearchStats stats;
    // Describes the meaning of score without changing its compatibility value.
    SearchScoreKind score_kind = SearchScoreKind::Heuristic;
    bool used_exact_endgame = false;
    std::optional<int> exact_disc_margin = std::nullopt;
};

struct IterativeSearchDepthInfo {
    int requested_depth = 0;
    int completed_depth = 0;
    std::optional<int> previous_score = std::nullopt;
    int score = 0;
    int previous_score_delta = 0;
    std::optional<Square> best_move;
    std::vector<Square> principal_variation;
    SearchStats stats;
    std::uint64_t elapsed_ns = 0;
};

using IterativeSearchDepthObserver = void (*)(const IterativeSearchDepthInfo& info,
                                              void* user_data);

struct RootMoveOrderingEntry {
    Square move;
    int order_score = 0;
};

struct SearchOptions {
    int max_depth = 5;
    bool use_transposition_table = false;
    // Approximate requested midgame transposition table entries. The internal
    // table may round this to a bucketed power-of-two capacity.
    std::size_t transposition_table_entries = 1 << 18;
    // Store depth-0 midgame heuristic leaves in the transposition table. The
    // default preserves the current search behavior for ablation comparisons.
    bool store_leaf_tt_entries = true;
    // Root-only exact endgame cutoff by empty square count for FixedThreshold.
    // Values <= 0 disable root exact solving for every root policy.
    int exact_endgame_empty_threshold = 12;
    // Adaptive16 is an experimental root-only profile: <=14 empties solve
    // exactly, while 15/16 empties solve only for conservative low-branching
    // roots with bounded opponent mobility.
    ExactEndgameRootPolicy exact_endgame_root_policy = ExactEndgameRootPolicy::FixedThreshold;
    // Optional requested private exact-endgame TT entries for root exact solves.
    // nullopt keeps the exact solver's root-empty-count based default; 0 disables
    // only the exact solver TT.
    std::optional<std::size_t> exact_endgame_tt_entries = std::nullopt;
    bool use_pvs = false;
    // Opt-in iterative-search aspiration window. Fixed-depth search ignores
    // these fields; iterative search still uses a full window for depth 1.
    bool use_aspiration_window = false;
    int aspiration_window = 50;
    int aspiration_max_researches = 4;
    AspirationProfile aspiration_profile = AspirationProfile::Fixed;
    std::optional<EvaluationConfig> evaluation_config_override = std::nullopt;
    // Optional instrumentation hooks used by benchmark tools. Null defaults keep
    // public search behavior and normal result payloads unchanged.
    IterativeSearchDepthObserver iterative_depth_observer = nullptr;
    void* iterative_depth_observer_user_data = nullptr;
    std::vector<RootMoveOrderingEntry>* root_move_ordering_snapshot = nullptr;
};

class SearchSession {
public:
    SearchSession();
    explicit SearchSession(const SearchOptions& options);
    SearchSession(SearchSession&&) noexcept;
    SearchSession& operator=(SearchSession&&) noexcept;
    SearchSession(const SearchSession&) = delete;
    SearchSession& operator=(const SearchSession&) = delete;
    ~SearchSession();

    void reset() noexcept;

    [[nodiscard]] std::uint32_t generation() const noexcept;
    [[nodiscard]] SearchMode mode() const noexcept;
    [[nodiscard]] std::optional<Square> previous_best_move() const noexcept;
    [[nodiscard]] std::vector<Square> root_principal_variation() const;

private:
    std::unique_ptr<search_detail::SearchSessionState> state_;

    friend SearchResult search(SearchSession& session, const Board& board,
                               const SearchOptions& options) noexcept;
    friend SearchResult search_iterative(SearchSession& session, const Board& board,
                                         const SearchOptions& options) noexcept;
};

[[nodiscard]] EvaluationConfig resolve_evaluation_config(const SearchOptions& options) noexcept;
[[nodiscard]] ExactEndgameRootDecision
decide_exact_endgame_root(const Board& board, const SearchOptions& options) noexcept;
[[nodiscard]] SearchResult search(const Board& board, const SearchOptions& options) noexcept;
[[nodiscard]] SearchResult search(SearchSession& session, const Board& board,
                                  const SearchOptions& options) noexcept;
[[nodiscard]] SearchResult search_fixed_depth(const Board& board, int depth) noexcept;
[[nodiscard]] SearchResult search_iterative(const Board& board,
                                            const SearchOptions& options) noexcept;
[[nodiscard]] SearchResult search_iterative(SearchSession& session, const Board& board,
                                            const SearchOptions& options) noexcept;

} // namespace othello
