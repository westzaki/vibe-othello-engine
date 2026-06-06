#pragma once

#include "search_common.hpp"
#include "search_ordering.hpp"
#include "search_runtime_options.hpp"
#include "search_tt.hpp"

#include <cstdint>
#include <optional>
#include <othello/evaluation.hpp>
#include <othello/hash.hpp>
#include <othello/search.hpp>

namespace othello {

[[nodiscard]] EvaluationConfig resolve_evaluation_config(const SearchOptions& options) noexcept;

namespace search_detail {

struct SearchSessionState {
    SearchEngineOptions engine_options{};
    EvaluationConfig evaluation_config = default_evaluation_config();
    std::uint64_t evaluation_identity = 0;
    std::uint32_t generation = 0;
    SearchMode mode = SearchMode::FixedDepth;
    TranspositionTable transpositions;
    PrincipalVariation root_principal_variation;
    std::optional<ZobristHash> root_hash = std::nullopt;
    std::optional<Square> previous_best_move = std::nullopt;
    std::optional<int> previous_score = std::nullopt;
    std::optional<int> previous_score_delta = std::nullopt;
    HistoryKillerState history_killers;

    SearchSessionState() noexcept;
    explicit SearchSessionState(const SearchOptions& options) noexcept;

    void reset() noexcept;
};

void begin_session_search(SearchSessionState& session,
                          const SearchEngineOptions& engine_options,
                          EvaluationConfig evaluation_config, SearchMode mode) noexcept;

void finish_session_search(SearchSessionState& session, ZobristHash root_hash,
                           const SearchResult& result) noexcept;

} // namespace search_detail
} // namespace othello
