#include <othello/evaluation.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace othello {
namespace {

struct EvaluationPresetSpec {
    EvaluationPreset preset;
    std::string_view name;
    EvaluationConfig (*config)() noexcept;
    // Stable public presets are intended long-lived built-ins. Entries with
    // stable_public=false are legacy experimental aliases: keep them selectable
    // and behavior-compatible, but do not add new ones for ordinary evaluator
    // experiments. New experimental weight sets should live in .eval configs.
    bool stable_public = false;
};

[[nodiscard]] EvaluationConfig phase_aware_v1_evaluation_config() noexcept {
    return EvaluationConfig{
        .opening = EvaluationFeatureWeights{
            .disc_difference = 0,
            .mobility = 8,
            .potential_mobility = 4,
            .corner_occupancy = 35,
            .corner_access = 30,
            .x_square_danger = 25,
            .frontier = 3,
        },
        .midgame = EvaluationFeatureWeights{
            .disc_difference = 1,
            .mobility = 10,
            .potential_mobility = 5,
            .corner_occupancy = 40,
            .corner_access = 35,
            .x_square_danger = 30,
            .frontier = 4,
        },
        .late = EvaluationFeatureWeights{
            .disc_difference = 4,
            .mobility = 6,
            .potential_mobility = 2,
            .corner_occupancy = 45,
            .corner_access = 20,
            .x_square_danger = 20,
            .frontier = 2,
        },
        .opening_max_occupied = 20,
        .midgame_max_occupied = 44,
    };
}

[[nodiscard]] EvaluationConfig mobility_plus_smoke_evaluation_config() noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.mobility = 10;
    config.midgame.mobility = 12;
    config.late.mobility = 8;
    return config;
}

[[nodiscard]] EvaluationConfig frontier_open2_mid2_late_plus1_evaluation_config()
    noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.frontier = 5;
    config.midgame.frontier = 6;
    config.late.frontier = 3;
    return config;
}

[[nodiscard]] EvaluationConfig classic_corner_lite_v1_evaluation_config() noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.corner_local_2x3 = 8;
    config.midgame.corner_local_2x3 = 10;
    config.late.corner_local_2x3 = 6;
    return config;
}

[[nodiscard]] EvaluationConfig classic_edge_lite_v1_evaluation_config() noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.edge_stability_lite = 2;
    config.midgame.edge_stability_lite = 4;
    config.late.edge_stability_lite = 8;
    return config;
}

[[nodiscard]] EvaluationConfig classic_features_lite_v1_evaluation_config() noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.corner_local_2x3 = 8;
    config.midgame.corner_local_2x3 = 10;
    config.late.corner_local_2x3 = 6;
    config.opening.edge_stability_lite = 2;
    config.midgame.edge_stability_lite = 4;
    config.late.edge_stability_lite = 8;
    return config;
}

[[nodiscard]] EvaluationConfig classic_features_lite_aggressive_evaluation_config()
    noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.corner_local_2x3 = 14;
    config.midgame.corner_local_2x3 = 18;
    config.late.corner_local_2x3 = 10;
    config.opening.edge_stability_lite = 4;
    config.midgame.edge_stability_lite = 8;
    config.late.edge_stability_lite = 12;
    return config;
}

[[nodiscard]] EvaluationConfig frontier_classic_features_lite_v1_evaluation_config()
    noexcept {
    EvaluationConfig config = frontier_open2_mid2_late_plus1_evaluation_config();
    config.opening.corner_local_2x3 = 8;
    config.midgame.corner_local_2x3 = 10;
    config.late.corner_local_2x3 = 6;
    config.opening.edge_stability_lite = 2;
    config.midgame.edge_stability_lite = 4;
    config.late.edge_stability_lite = 8;
    return config;
}

[[nodiscard]] EvaluationConfig corner_pattern_2x3_v1_evaluation_config() noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.corner_2x3_pattern = 4;
    config.midgame.corner_2x3_pattern = 6;
    config.late.corner_2x3_pattern = 4;
    return config;
}

[[nodiscard]] EvaluationConfig corner_pattern_2x3_aggressive_evaluation_config()
    noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.corner_2x3_pattern = 8;
    config.midgame.corner_2x3_pattern = 10;
    config.late.corner_2x3_pattern = 6;
    return config;
}

[[nodiscard]] EvaluationConfig frontier_corner_pattern_2x3_v1_evaluation_config()
    noexcept {
    EvaluationConfig config = frontier_open2_mid2_late_plus1_evaluation_config();
    config.opening.corner_2x3_pattern = 4;
    config.midgame.corner_2x3_pattern = 6;
    config.late.corner_2x3_pattern = 4;
    return config;
}

[[nodiscard]] EvaluationConfig frontier_corner_pattern_edge_lite_v1_evaluation_config()
    noexcept {
    EvaluationConfig config = frontier_corner_pattern_2x3_v1_evaluation_config();
    config.opening.edge_stability_lite = 2;
    config.midgame.edge_stability_lite = 4;
    config.late.edge_stability_lite = 8;
    return config;
}

[[nodiscard]] EvaluationConfig edge_pattern_8_v1_evaluation_config() noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.edge_8_pattern = 2;
    config.midgame.edge_8_pattern = 4;
    config.late.edge_8_pattern = 6;
    return config;
}

[[nodiscard]] EvaluationConfig edge_pattern_8_aggressive_evaluation_config() noexcept {
    EvaluationConfig config = phase_aware_v1_evaluation_config();
    config.opening.edge_8_pattern = 4;
    config.midgame.edge_8_pattern = 8;
    config.late.edge_8_pattern = 10;
    return config;
}

[[nodiscard]] EvaluationConfig default_edge_pattern_8_v1_evaluation_config() noexcept {
    EvaluationConfig config = default_evaluation_config();
    config.opening.edge_8_pattern = 2;
    config.midgame.edge_8_pattern = 4;
    config.late.edge_8_pattern = 6;
    return config;
}

[[nodiscard]] EvaluationConfig default_edge_pattern_8_no_edge_lite_evaluation_config()
    noexcept {
    EvaluationConfig config = default_evaluation_config();
    config.opening.edge_stability_lite = 0;
    config.midgame.edge_stability_lite = 0;
    config.late.edge_stability_lite = 0;
    config.opening.edge_8_pattern = 2;
    config.midgame.edge_8_pattern = 4;
    config.late.edge_8_pattern = 6;
    return config;
}

[[nodiscard]] EvaluationConfig default_edge_pattern_8_aggressive_evaluation_config()
    noexcept {
    EvaluationConfig config = default_evaluation_config();
    config.opening.edge_8_pattern = 4;
    config.midgame.edge_8_pattern = 8;
    config.late.edge_8_pattern = 10;
    return config;
}

// The first entry for a preset is its canonical name; later duplicate preset
// entries are accepted legacy aliases. Default and PhaseAwareV1 are stable
// public built-ins. The other entries originated as experiments and remain here
// only as compatibility names for existing tools, tests, and reports.
//
// New evaluator experiments should be added as .eval files and selected with
// --eval-config. Do not grow this table as an experiment registry.
constexpr std::array<EvaluationPresetSpec, 19> evaluation_preset_specs{{
    {.preset = EvaluationPreset::Default,
     .name = "default",
     .config = default_evaluation_config,
     .stable_public = true},
    {.preset = EvaluationPreset::PhaseAwareV1,
     .name = "phase_aware_v1",
     .config = phase_aware_v1_evaluation_config,
     .stable_public = true},
    {.preset = EvaluationPreset::PhaseAwareV1,
     .name = "legacy_phase_aware_v1",
     .config = phase_aware_v1_evaluation_config,
     .stable_public = true},
    {.preset = EvaluationPreset::MobilityPlusSmoke,
     .name = "mobility_plus_smoke",
     .config = mobility_plus_smoke_evaluation_config},
    {.preset = EvaluationPreset::FrontierOpen2Mid2LatePlus1,
     .name = "frontier_open2_mid2_late_plus1",
     .config = frontier_open2_mid2_late_plus1_evaluation_config},
    {.preset = EvaluationPreset::ClassicCornerLiteV1,
     .name = "classic_corner_lite_v1",
     .config = classic_corner_lite_v1_evaluation_config},
    {.preset = EvaluationPreset::ClassicEdgeLiteV1,
     .name = "classic_edge_lite_v1",
     .config = classic_edge_lite_v1_evaluation_config},
    {.preset = EvaluationPreset::ClassicFeaturesLiteV1,
     .name = "classic_features_lite_v1",
     .config = classic_features_lite_v1_evaluation_config},
    {.preset = EvaluationPreset::ClassicFeaturesLiteAggressive,
     .name = "classic_features_lite_aggressive",
     .config = classic_features_lite_aggressive_evaluation_config},
    {.preset = EvaluationPreset::FrontierClassicFeaturesLiteV1,
     .name = "frontier_classic_features_lite_v1",
     .config = frontier_classic_features_lite_v1_evaluation_config},
    {.preset = EvaluationPreset::CornerPattern2x3V1,
     .name = "corner_pattern_2x3_v1",
     .config = corner_pattern_2x3_v1_evaluation_config},
    {.preset = EvaluationPreset::CornerPattern2x3Aggressive,
     .name = "corner_pattern_2x3_aggressive",
     .config = corner_pattern_2x3_aggressive_evaluation_config},
    {.preset = EvaluationPreset::FrontierCornerPattern2x3V1,
     .name = "frontier_corner_pattern_2x3_v1",
     .config = frontier_corner_pattern_2x3_v1_evaluation_config},
    {.preset = EvaluationPreset::FrontierCornerPatternEdgeLiteV1,
     .name = "frontier_corner_pattern_edge_lite_v1",
     .config = frontier_corner_pattern_edge_lite_v1_evaluation_config},
    {.preset = EvaluationPreset::EdgePattern8V1,
     .name = "edge_pattern_8_v1",
     .config = edge_pattern_8_v1_evaluation_config},
    {.preset = EvaluationPreset::EdgePattern8Aggressive,
     .name = "edge_pattern_8_aggressive",
     .config = edge_pattern_8_aggressive_evaluation_config},
    {.preset = EvaluationPreset::DefaultEdgePattern8V1,
     .name = "default_edge_pattern_8_v1",
     .config = default_edge_pattern_8_v1_evaluation_config},
    {.preset = EvaluationPreset::DefaultEdgePattern8NoEdgeLite,
     .name = "default_edge_pattern_8_no_edge_lite",
     .config = default_edge_pattern_8_no_edge_lite_evaluation_config},
    {.preset = EvaluationPreset::DefaultEdgePattern8Aggressive,
     .name = "default_edge_pattern_8_aggressive",
     .config = default_edge_pattern_8_aggressive_evaluation_config},
}};

static_assert(evaluation_preset_specs[0].stable_public);
static_assert(evaluation_preset_specs[1].stable_public);

[[nodiscard]] constexpr const EvaluationPresetSpec*
find_preset_spec(EvaluationPreset preset) noexcept {
    for (const EvaluationPresetSpec& spec : evaluation_preset_specs) {
        if (spec.preset == preset) {
            return &spec;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr const EvaluationPresetSpec*
find_preset_spec(std::string_view name) noexcept {
    for (const EvaluationPresetSpec& spec : evaluation_preset_specs) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace

EvaluationConfig evaluation_config_for_preset(EvaluationPreset preset) noexcept {
    const EvaluationPresetSpec* const spec = find_preset_spec(preset);
    if (spec == nullptr) {
        return phase_aware_v1_evaluation_config();
    }
    return spec->config();
}

std::optional<EvaluationPreset> evaluation_preset_from_name(std::string_view name) noexcept {
    const EvaluationPresetSpec* const spec = find_preset_spec(name);
    if (spec == nullptr) {
        return std::nullopt;
    }
    return spec->preset;
}

std::string_view evaluation_preset_name(EvaluationPreset preset) noexcept {
    const EvaluationPresetSpec* const spec = find_preset_spec(preset);
    if (spec == nullptr) {
        return "unknown";
    }
    return spec->name;
}

} // namespace othello
