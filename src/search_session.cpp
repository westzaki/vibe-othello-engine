#include "search_session.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <othello/evaluation_feature_specs.hpp>
#include <utility>

namespace othello {

EvaluationConfig resolve_evaluation_config(const SearchOptions& options) noexcept {
    if (options.evaluation_config_override.has_value()) {
        return *options.evaluation_config_override;
    }
    return default_evaluation_config();
}

namespace search_detail {

namespace {

[[nodiscard]] constexpr std::uint64_t hash_mix(std::uint64_t hash,
                                                std::uint64_t value) noexcept {
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

[[nodiscard]] constexpr std::uint64_t hash_int(std::uint64_t hash, int value) noexcept {
    return hash_mix(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

[[nodiscard]] constexpr std::uint64_t
evaluation_weights_identity(std::uint64_t hash,
                            const EvaluationFeatureWeights& weights) noexcept {
    for (const evaluation_detail::EvaluationFeatureSpec& spec :
         evaluation_detail::evaluation_feature_specs) {
        hash = hash_int(hash, weights.*spec.weight);
    }
    return hash;
}

template <std::size_t Size>
[[nodiscard]] std::uint64_t table_identity(std::uint64_t hash,
                                           const std::array<std::int16_t, Size>& values) noexcept {
    for (std::int16_t value : values) {
        hash = hash_int(hash, value);
    }
    return hash;
}

[[nodiscard]] std::uint64_t pattern_tables_identity(
    std::uint64_t hash, const std::shared_ptr<const PatternTableBundle>& tables) noexcept {
    if (tables == nullptr) {
        return hash_mix(hash, 0);
    }

    hash = hash_mix(hash, 1);
    hash = table_identity(hash, tables->corner_2x3);
    hash = table_identity(hash, tables->corner_3x3);
    hash = table_identity(hash, tables->edge_8);
    hash = table_identity(hash, tables->edge_x_10);
    hash = table_identity(hash, tables->diagonal_8);
    hash = table_identity(hash, tables->inner_row_8);
    return hash;
}

[[nodiscard]] std::uint64_t evaluation_config_identity(const EvaluationConfig& config) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    hash = evaluation_weights_identity(hash, config.opening);
    hash = evaluation_weights_identity(hash, config.midgame);
    hash = evaluation_weights_identity(hash, config.late);
    hash = hash_int(hash, config.opening_max_occupied);
    hash = hash_int(hash, config.midgame_max_occupied);
    hash = pattern_tables_identity(hash, config.pattern_tables);
    hash = pattern_tables_identity(hash, config.opening_pattern_tables);
    hash = pattern_tables_identity(hash, config.midgame_pattern_tables);
    hash = pattern_tables_identity(hash, config.late_pattern_tables);
    return hash;
}

[[nodiscard]] constexpr bool requires_new_transposition_table(
    const SearchEngineOptions& current, const SearchEngineOptions& next) noexcept {
    return current.use_transposition_table != next.use_transposition_table ||
           current.transposition_table_entries != next.transposition_table_entries;
}

} // namespace

SearchSessionState::SearchSessionState() noexcept : transpositions{engine_options} {}

SearchSessionState::SearchSessionState(const SearchOptions& options) noexcept
    : engine_options{engine_options_from(options)},
      evaluation_config{resolve_evaluation_config(options)},
      transpositions{engine_options} {}

void SearchSessionState::reset() noexcept {
    engine_options = SearchEngineOptions{};
    evaluation_config = default_evaluation_config();
    evaluation_identity = 0;
    generation = 0;
    mode = SearchMode::FixedDepth;
    transpositions = TranspositionTable{engine_options};
    root_principal_variation = PrincipalVariation{};
    root_hash = std::nullopt;
    previous_best_move = std::nullopt;
    previous_score = std::nullopt;
    previous_score_delta = std::nullopt;
    history_killers.reset();
}

void begin_session_search(SearchSessionState& session, const SearchEngineOptions& engine_options,
                          EvaluationConfig evaluation_config, SearchMode mode) noexcept {
    if (requires_new_transposition_table(session.engine_options, engine_options)) {
        session.transpositions = TranspositionTable{engine_options};
    }
    session.engine_options = engine_options;
    session.evaluation_config = std::move(evaluation_config);
    session.evaluation_identity = evaluation_config_identity(session.evaluation_config);
    session.mode = mode;
    ++session.generation;
    if (session.generation == 0) {
        session.generation = 1;
    }
}

void finish_session_search(SearchSessionState& session, ZobristHash root_hash,
                           const SearchResult& result) noexcept {
    session.previous_score_delta = session.previous_score.has_value()
                                       ? std::optional<int>{result.score - *session.previous_score}
                                       : std::nullopt;
    session.previous_score = result.score;
    session.previous_best_move = result.best_move;
    session.root_hash = root_hash;
    session.root_principal_variation =
        principal_variation_from_vector(result.principal_variation);
}

} // namespace search_detail
} // namespace othello
