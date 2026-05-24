#include "summary.hpp"

#include <span>
#include <string>
#include <vector>

namespace othello::match_summary {
namespace {

[[nodiscard]] bool contains_string(std::span<const std::string> values,
                                   const std::string& value) {
    for (const std::string& existing : values) {
        if (existing == value) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t opening_summary_index(std::vector<OpeningSummary>& openings,
                                                const GameRecord& record) {
    for (std::size_t index = 0; index < openings.size(); ++index) {
        if (openings[index].opening_index == record.opening_index &&
            openings[index].opening_name == record.opening_name) {
            return index;
        }
    }

    openings.push_back(OpeningSummary{.opening_index = record.opening_index,
                                      .opening_name = record.opening_name});
    return openings.size() - 1;
}

} // namespace

Summary summarize(std::span<const GameRecord> records) {
    Summary summary;
    summary.games = static_cast<int>(records.size());

    int total_disc_diff = 0;
    int total_plies = 0;
    int total_passes = 0;
    std::vector<int> opening_diff_totals;

    for (const GameRecord& record : records) {
        if (!contains_string(summary.player_a_specs, record.player_a_spec)) {
            summary.player_a_specs.push_back(record.player_a_spec);
        }
        if (!contains_string(summary.player_b_specs, record.player_b_spec)) {
            summary.player_b_specs.push_back(record.player_b_spec);
        }

        const std::size_t opening_index = opening_summary_index(summary.openings, record);
        if (opening_diff_totals.size() < summary.openings.size()) {
            opening_diff_totals.push_back(0);
        }
        OpeningSummary& opening = summary.openings[opening_index];
        ++opening.games;

        if (record.illegal_or_error) {
            ++summary.error_games;
            ++opening.error_games;
            continue;
        }

        ++summary.valid_games;
        ++opening.valid_games;
        total_disc_diff += record.score_diff_from_player_a;
        total_plies += record.plies;
        total_passes += record.passes;
        opening_diff_totals[opening_index] += record.score_diff_from_player_a;

        if (record.score_diff_from_player_a > 0) {
            ++summary.player_a_wins;
            ++opening.player_a_wins;
        } else if (record.score_diff_from_player_a < 0) {
            ++summary.player_b_wins;
            ++opening.player_b_wins;
        } else {
            ++summary.draws;
            ++opening.draws;
        }
    }

    summary.unique_openings_count = static_cast<int>(summary.openings.size());
    if (summary.valid_games > 0) {
        const double valid_games = static_cast<double>(summary.valid_games);
        summary.player_a_win_rate = static_cast<double>(summary.player_a_wins) / valid_games;
        summary.player_b_win_rate = static_cast<double>(summary.player_b_wins) / valid_games;
        summary.average_disc_diff_from_player_a = static_cast<double>(total_disc_diff) / valid_games;
        summary.average_plies = static_cast<double>(total_plies) / valid_games;
        summary.average_passes = static_cast<double>(total_passes) / valid_games;
    }

    for (std::size_t index = 0; index < summary.openings.size(); ++index) {
        OpeningSummary& opening = summary.openings[index];
        if (opening.valid_games > 0) {
            opening.average_disc_diff_from_player_a =
                static_cast<double>(opening_diff_totals[index]) /
                static_cast<double>(opening.valid_games);
        }
    }

    return summary;
}

} // namespace othello::match_summary
