#include <bit>
#include <othello/board.hpp>
#include <othello/evaluation.hpp>
#include <othello/rules.hpp>

#include <array>

namespace othello {
namespace {

constexpr Bitboard a_file = 0x0101010101010101ULL;
constexpr Bitboard h_file = 0x8080808080808080ULL;
constexpr Bitboard not_a_file = ~a_file;
constexpr Bitboard not_h_file = ~h_file;

constexpr Bitboard corner_squares =
    (Bitboard{1} << 0) | (Bitboard{1} << 7) | (Bitboard{1} << 56) | (Bitboard{1} << 63);

constexpr int terminal_score_weight = 1000;

struct CornerLocalSpec {
    int corner_index = 0;
    int x_square_index = 0;
    std::array<int, 2> c_square_indexes{};
};

struct EdgeRaySpec {
    int first_index = 0;
    int step = 0;
};

struct EdgeCornerSpec {
    int corner_index = 0;
    std::array<EdgeRaySpec, 2> rays{};
};

constexpr std::array<CornerLocalSpec, 4> corner_local_specs{{
    {.corner_index = 0, .x_square_index = 9, .c_square_indexes = {1, 8}},
    {.corner_index = 7, .x_square_index = 14, .c_square_indexes = {6, 15}},
    {.corner_index = 56, .x_square_index = 49, .c_square_indexes = {57, 48}},
    {.corner_index = 63, .x_square_index = 54, .c_square_indexes = {62, 55}},
}};

constexpr std::array<EdgeCornerSpec, 4> edge_corner_specs{{
    {.corner_index = 0, .rays = {{{.first_index = 1, .step = 1},
                                  {.first_index = 8, .step = 8}}}},
    {.corner_index = 7, .rays = {{{.first_index = 6, .step = -1},
                                  {.first_index = 15, .step = 8}}}},
    {.corner_index = 56, .rays = {{{.first_index = 57, .step = 1},
                                   {.first_index = 48, .step = -8}}}},
    {.corner_index = 63, .rays = {{{.first_index = 62, .step = -1},
                                   {.first_index = 55, .step = -8}}}},
}};

[[nodiscard]] constexpr Bitboard square_bit(int index) noexcept {
    return Bitboard{1} << index;
}

[[nodiscard]] constexpr Board with_side_to_move(const Board& board, Side side) noexcept {
    return Board{
        .black = board.black,
        .white = board.white,
        .side_to_move = side,
    };
}

[[nodiscard]] constexpr Bitboard shift_east(Bitboard bits) noexcept {
    return (bits & not_h_file) << 1;
}

[[nodiscard]] constexpr Bitboard shift_west(Bitboard bits) noexcept {
    return (bits & not_a_file) >> 1;
}

[[nodiscard]] constexpr Bitboard shift_north(Bitboard bits) noexcept {
    return bits << 8;
}

[[nodiscard]] constexpr Bitboard shift_south(Bitboard bits) noexcept {
    return bits >> 8;
}

[[nodiscard]] constexpr Bitboard shift_northeast(Bitboard bits) noexcept {
    return (bits & not_h_file) << 9;
}

[[nodiscard]] constexpr Bitboard shift_northwest(Bitboard bits) noexcept {
    return (bits & not_a_file) << 7;
}

[[nodiscard]] constexpr Bitboard shift_southeast(Bitboard bits) noexcept {
    return (bits & not_h_file) >> 7;
}

[[nodiscard]] constexpr Bitboard shift_southwest(Bitboard bits) noexcept {
    return (bits & not_a_file) >> 9;
}

[[nodiscard]] constexpr Bitboard adjacent_squares(Bitboard bits) noexcept {
    return shift_east(bits) | shift_west(bits) | shift_north(bits) | shift_south(bits) |
           shift_northeast(bits) | shift_northwest(bits) | shift_southeast(bits) |
           shift_southwest(bits);
}

[[nodiscard]] EvaluationPhase phase_for_occupied_count(
    int occupied_count, const EvaluationConfig& config) noexcept {
    if (occupied_count <= config.opening_max_occupied) {
        return EvaluationPhase::Opening;
    }
    if (occupied_count <= config.midgame_max_occupied) {
        return EvaluationPhase::Midgame;
    }
    return EvaluationPhase::Late;
}

[[nodiscard]] constexpr const EvaluationFeatureWeights&
weights_for_phase(const EvaluationConfig& config, EvaluationPhase phase) noexcept {
    switch (phase) {
    case EvaluationPhase::Opening:
        return config.opening;
    case EvaluationPhase::Midgame:
        return config.midgame;
    case EvaluationPhase::Late:
        return config.late;
    }

    return config.opening;
}

[[nodiscard]] int legal_move_count(const Board& board, Side side) noexcept {
    return std::popcount(legal_moves(with_side_to_move(board, side)));
}

[[nodiscard]] int corner_count(const Board& board, Side side) noexcept {
    return std::popcount(board.discs(side) & corner_squares);
}

[[nodiscard]] int legal_corner_move_count(const Board& board, Side side) noexcept {
    return std::popcount(legal_moves(with_side_to_move(board, side)) & corner_squares);
}

[[nodiscard]] int potential_mobility_count(const Board& board, Side side) noexcept {
    const Bitboard empty_squares = ~board.occupied();
    return std::popcount(adjacent_squares(board.discs(opponent(side))) & empty_squares);
}

[[nodiscard]] int frontier_count(const Board& board, Side side) noexcept {
    const Bitboard frontier_squares = adjacent_squares(~board.occupied());
    return std::popcount(board.discs(side) & frontier_squares);
}

[[nodiscard]] int dangerous_x_square_count(const Board& board, Side side) noexcept {
    int count = 0;
    const Bitboard occupied = board.occupied();
    const Bitboard discs = board.discs(side);

    const auto count_if_dangerous = [&](int corner_index, int x_square_index) noexcept {
        const Bitboard corner = Bitboard{1} << corner_index;
        const Bitboard x_square = Bitboard{1} << x_square_index;
        if ((occupied & corner) == 0 && (discs & x_square) != 0) {
            ++count;
        }
    };

    count_if_dangerous(0, 9);   // a1 / b2
    count_if_dangerous(7, 14);  // h1 / g2
    count_if_dangerous(56, 49); // a8 / b7
    count_if_dangerous(63, 54); // h8 / g7
    return count;
}

[[nodiscard]] int corner_local_2x3_value_for_side(const Board& board, Side side) noexcept {
    int value = 0;
    const Bitboard occupied = board.occupied();
    const Bitboard discs = board.discs(side);

    for (const CornerLocalSpec& spec : corner_local_specs) {
        const Bitboard corner = square_bit(spec.corner_index);
        if ((occupied & corner) == 0) {
            if ((discs & square_bit(spec.x_square_index)) != 0) {
                value -= 2;
            }
            for (const int c_square_index : spec.c_square_indexes) {
                if ((discs & square_bit(c_square_index)) != 0) {
                    --value;
                }
            }
            continue;
        }

        if ((discs & corner) != 0) {
            for (const int c_square_index : spec.c_square_indexes) {
                if ((discs & square_bit(c_square_index)) != 0) {
                    ++value;
                }
            }
        }
    }

    return value;
}

[[nodiscard]] int corner_local_2x3_score(const Board& board, Side side) noexcept {
    return corner_local_2x3_value_for_side(board, side) -
           corner_local_2x3_value_for_side(board, opponent(side));
}

[[nodiscard]] Bitboard anchored_edge_ray_squares(const Board& board, Side side,
                                                 const EdgeRaySpec& ray) noexcept {
    Bitboard squares = 0;
    const Bitboard discs = board.discs(side);
    int index = ray.first_index;
    for (int distance = 0; distance < 7; ++distance) {
        const Bitboard square = square_bit(index);
        if ((discs & square) == 0) {
            break;
        }
        squares |= square;
        index += ray.step;
    }
    return squares;
}

[[nodiscard]] int edge_stability_lite_value_for_side(const Board& board, Side side) noexcept {
    Bitboard stable_edge_squares = 0;
    const Bitboard discs = board.discs(side);

    // This is a stability-lite approximation: collect continuous edge rays
    // anchored at owned corners, then count unique found squares only once. A
    // ray excludes its anchor corner but may include the opposite corner when
    // the whole edge is continuous.
    for (const EdgeCornerSpec& spec : edge_corner_specs) {
        if ((discs & square_bit(spec.corner_index)) == 0) {
            continue;
        }
        for (const EdgeRaySpec& ray : spec.rays) {
            stable_edge_squares |= anchored_edge_ray_squares(board, side, ray);
        }
    }

    return std::popcount(stable_edge_squares);
}

[[nodiscard]] int edge_stability_lite_score(const Board& board, Side side) noexcept {
    return edge_stability_lite_value_for_side(board, side) -
           edge_stability_lite_value_for_side(board, opponent(side));
}

} // namespace

int evaluate_disc_difference(const Board& board, Side side) noexcept {
    return score(board, side);
}

int evaluate_mobility(const Board& board, Side side) noexcept {
    return legal_move_count(board, side) - legal_move_count(board, opponent(side));
}

EvaluationConfig evaluation_config_for_preset(EvaluationPreset preset) noexcept {
    EvaluationConfig config = default_evaluation_config();
    switch (preset) {
    case EvaluationPreset::Default:
        return config;
    case EvaluationPreset::MobilityPlusSmoke:
        config.opening.mobility = 10;
        config.midgame.mobility = 12;
        config.late.mobility = 8;
        return config;
    case EvaluationPreset::FrontierOpen2Mid2LatePlus1:
        config.opening.frontier = 5;
        config.midgame.frontier = 6;
        config.late.frontier = 3;
        return config;
    case EvaluationPreset::ClassicCornerLiteV1:
        config.opening.corner_local_2x3 = 8;
        config.midgame.corner_local_2x3 = 10;
        config.late.corner_local_2x3 = 6;
        return config;
    case EvaluationPreset::ClassicEdgeLiteV1:
        config.opening.edge_stability_lite = 2;
        config.midgame.edge_stability_lite = 4;
        config.late.edge_stability_lite = 8;
        return config;
    case EvaluationPreset::ClassicFeaturesLiteV1:
        config.opening.corner_local_2x3 = 8;
        config.midgame.corner_local_2x3 = 10;
        config.late.corner_local_2x3 = 6;
        config.opening.edge_stability_lite = 2;
        config.midgame.edge_stability_lite = 4;
        config.late.edge_stability_lite = 8;
        return config;
    case EvaluationPreset::ClassicFeaturesLiteAggressive:
        config.opening.corner_local_2x3 = 14;
        config.midgame.corner_local_2x3 = 18;
        config.late.corner_local_2x3 = 10;
        config.opening.edge_stability_lite = 4;
        config.midgame.edge_stability_lite = 8;
        config.late.edge_stability_lite = 12;
        return config;
    case EvaluationPreset::FrontierClassicFeaturesLiteV1:
        config.opening.frontier = 5;
        config.midgame.frontier = 6;
        config.late.frontier = 3;
        config.opening.corner_local_2x3 = 8;
        config.midgame.corner_local_2x3 = 10;
        config.late.corner_local_2x3 = 6;
        config.opening.edge_stability_lite = 2;
        config.midgame.edge_stability_lite = 4;
        config.late.edge_stability_lite = 8;
        return config;
    }

    return config;
}

std::optional<EvaluationPreset> evaluation_preset_from_name(std::string_view name) noexcept {
    if (name == "default") {
        return EvaluationPreset::Default;
    }
    if (name == "mobility_plus_smoke") {
        return EvaluationPreset::MobilityPlusSmoke;
    }
    if (name == "frontier_open2_mid2_late_plus1") {
        return EvaluationPreset::FrontierOpen2Mid2LatePlus1;
    }
    if (name == "classic_corner_lite_v1") {
        return EvaluationPreset::ClassicCornerLiteV1;
    }
    if (name == "classic_edge_lite_v1") {
        return EvaluationPreset::ClassicEdgeLiteV1;
    }
    if (name == "classic_features_lite_v1") {
        return EvaluationPreset::ClassicFeaturesLiteV1;
    }
    if (name == "classic_features_lite_aggressive") {
        return EvaluationPreset::ClassicFeaturesLiteAggressive;
    }
    if (name == "frontier_classic_features_lite_v1") {
        return EvaluationPreset::FrontierClassicFeaturesLiteV1;
    }
    return std::nullopt;
}

std::string_view evaluation_preset_name(EvaluationPreset preset) noexcept {
    switch (preset) {
    case EvaluationPreset::Default:
        return "default";
    case EvaluationPreset::MobilityPlusSmoke:
        return "mobility_plus_smoke";
    case EvaluationPreset::FrontierOpen2Mid2LatePlus1:
        return "frontier_open2_mid2_late_plus1";
    case EvaluationPreset::ClassicCornerLiteV1:
        return "classic_corner_lite_v1";
    case EvaluationPreset::ClassicEdgeLiteV1:
        return "classic_edge_lite_v1";
    case EvaluationPreset::ClassicFeaturesLiteV1:
        return "classic_features_lite_v1";
    case EvaluationPreset::ClassicFeaturesLiteAggressive:
        return "classic_features_lite_aggressive";
    case EvaluationPreset::FrontierClassicFeaturesLiteV1:
        return "frontier_classic_features_lite_v1";
    }

    return "unknown";
}

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side,
                                             const EvaluationConfig& config) noexcept {
    const int occupied_count = std::popcount(board.occupied());
    const EvaluationPhase phase = phase_for_occupied_count(occupied_count, config);
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);

    EvaluationBreakdown breakdown{
        .phase = phase,
        .occupied_count = occupied_count,
        .empty_count = 64 - occupied_count,
        .disc_difference_weight = weights.disc_difference,
        .mobility_weight = weights.mobility,
        .corner_occupancy_weight = weights.corner_occupancy,
        .potential_mobility_weight = weights.potential_mobility,
        .corner_access_weight = weights.corner_access,
        .x_square_danger_weight = weights.x_square_danger,
        .frontier_weight = weights.frontier,
        .corner_local_2x3_weight = weights.corner_local_2x3,
        .edge_stability_lite_weight = weights.edge_stability_lite,
        .terminal_score_weight = terminal_score_weight,
    };

    if (is_game_over(board)) {
        breakdown.terminal = true;
        breakdown.terminal_disc_difference = score(board, side);
        breakdown.terminal_score =
            breakdown.terminal_disc_difference * breakdown.terminal_score_weight;
        breakdown.total = breakdown.terminal_score;
        return breakdown;
    }

    breakdown.disc_difference = evaluate_disc_difference(board, side);
    breakdown.disc_difference_score =
        breakdown.disc_difference * breakdown.disc_difference_weight;

    breakdown.mobility = evaluate_mobility(board, side);
    breakdown.mobility_score = breakdown.mobility * breakdown.mobility_weight;

    breakdown.corner_occupancy = corner_count(board, side) - corner_count(board, opponent(side));
    breakdown.corner_occupancy_score =
        breakdown.corner_occupancy * breakdown.corner_occupancy_weight;

    // Raw feature values are positive when they are good for `side`.
    breakdown.potential_mobility =
        potential_mobility_count(board, side) - potential_mobility_count(board, opponent(side));
    breakdown.potential_mobility_score =
        breakdown.potential_mobility * breakdown.potential_mobility_weight;

    breakdown.corner_access =
        legal_corner_move_count(board, side) - legal_corner_move_count(board, opponent(side));
    breakdown.corner_access_score = breakdown.corner_access * breakdown.corner_access_weight;

    // Owning an X-square next to an empty corner is dangerous, so the diff is
    // opponent dangerous X-squares minus our dangerous X-squares.
    breakdown.x_square_danger = dangerous_x_square_count(board, opponent(side)) -
                                dangerous_x_square_count(board, side);
    breakdown.x_square_danger_score =
        breakdown.x_square_danger * breakdown.x_square_danger_weight;

    // Fewer own frontier discs is usually better.
    breakdown.frontier = frontier_count(board, opponent(side)) - frontier_count(board, side);
    breakdown.frontier_score = breakdown.frontier * breakdown.frontier_weight;

    if (breakdown.corner_local_2x3_weight != 0) {
        breakdown.corner_local_2x3 = corner_local_2x3_score(board, side);
    }
    breakdown.corner_local_2x3_score =
        breakdown.corner_local_2x3 * breakdown.corner_local_2x3_weight;

    if (breakdown.edge_stability_lite_weight != 0) {
        breakdown.edge_stability_lite = edge_stability_lite_score(board, side);
    }
    breakdown.edge_stability_lite_score =
        breakdown.edge_stability_lite * breakdown.edge_stability_lite_weight;

    breakdown.total = breakdown.disc_difference_score + breakdown.mobility_score +
                      breakdown.corner_occupancy_score + breakdown.potential_mobility_score +
                      breakdown.corner_access_score + breakdown.x_square_danger_score +
                      breakdown.frontier_score + breakdown.corner_local_2x3_score +
                      breakdown.edge_stability_lite_score;
    return breakdown;
}

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side) noexcept {
    return evaluate_basic_breakdown(board, side, default_evaluation_config());
}

int evaluate_with_config(const Board& board, Side side, const EvaluationConfig& config) noexcept {
    return evaluate_basic_breakdown(board, side, config).total;
}

int evaluate_basic(const Board& board, Side side) noexcept {
    return evaluate_with_config(board, side, default_evaluation_config());
}

} // namespace othello
