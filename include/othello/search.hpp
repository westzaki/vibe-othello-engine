#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <othello/board.hpp>
#include <othello/evaluation.hpp>
#include <othello/search_stats.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello::search_detail {
struct SearchSessionState;
}

namespace othello {

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
    // Reserved for a future selective-search mode. Public so persisted session
    // diagnostics can name the mode once it is implemented.
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

struct SearchInstrumentation {
    // Optional hooks used by benchmark and diagnostic tools. Null defaults keep
    // normal search behavior and result payloads unchanged.
    IterativeSearchDepthObserver iterative_depth_observer = nullptr;
    void* iterative_depth_observer_user_data = nullptr;
    std::vector<RootMoveOrderingEntry>* root_move_ordering_snapshot = nullptr;
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
    // Minimum remaining search depth for midgame TT probes/stores. Zero keeps
    // the current behavior; positive values skip shallow TT work for ablation.
    int tt_min_probe_depth = 0;
    int tt_min_store_depth = 0;
    // Try a legal PV/root/TT preferred move before building the full midgame
    // ordering list. Default off preserves the existing eager ordering policy.
    bool use_lazy_first_move_ordering = false;
    // Allow a shallower matching TT entry to provide only a best-move ordering
    // hint. Cutoff lookups still require entry.depth >= requested depth.
    bool use_shallow_tt_move_ordering_hint = false;
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
    SearchInstrumentation instrumentation;
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
