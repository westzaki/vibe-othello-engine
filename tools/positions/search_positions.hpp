#pragma once

#include "positions/fixtures.hpp"

#include <optional>
#include <vector>

namespace othello::benchmarks {

[[nodiscard]] inline std::optional<std::vector<Position>> make_search_smoke_positions() {
    return make_fixed_positions();
}

[[nodiscard]] inline std::optional<std::vector<Position>> make_search_suite_positions() {
    std::vector<Position> positions;
    positions.reserve(25);

    if (!add_position(positions, "opening-a1-access",
                      "........\n"
                      ".B......\n"
                      "..B.B...\n"
                      "...BB...\n"
                      ".WWWWW..\n"
                      "..B.....\n"
                      "........\n"
                      "........\n"
                      "side=W",
                      "opening", "corner_available,x_square_risk",
                      "fixed-seed legal playout game 2317 ply 7")) {
        return std::nullopt;
    }

    if (!add_position(positions, "opening-central-run",
                      "........\n"
                      "........\n"
                      "....BBB.\n"
                      "...BBB..\n"
                      "...WW...\n"
                      "....W...\n"
                      "........\n"
                      "........\n"
                      "side=W",
                      "opening", "x_square_risk", "fixed-seed legal playout game 18 ply 5")) {
        return std::nullopt;
    }

    if (!add_position(positions, "opening-wide-mobility",
                      "........\n"
                      "........\n"
                      "..B..B..\n"
                      "..WWWW..\n"
                      "..WBB...\n"
                      "..B.....\n"
                      ".B......\n"
                      "........\n"
                      "side=W",
                      "opening", "high_mobility,corner_available,x_square_risk",
                      "fixed-seed legal playout game 5604 ply 7")) {
        return std::nullopt;
    }

    if (!add_position(positions, "opening-quiet-development",
                      "........\n"
                      ".....BW.\n"
                      ".....W..\n"
                      "...BWB..\n"
                      "...WB...\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B",
                      "opening", {}, "fixed-seed legal playout game 37 ply 4")) {
        return std::nullopt;
    }

    if (!add_position(positions, "early-black-pass-wing",
                      "W.......\n"
                      "W.......\n"
                      "WB..B...\n"
                      "WWBBB...\n"
                      "WBBBB...\n"
                      "..B.B...\n"
                      "....B...\n"
                      "........\n"
                      "side=B",
                      "early_midgame", "low_mobility,pass",
                      "fixed-seed legal playout game 2164 ply 14")) {
        return std::nullopt;
    }

    if (!add_position(positions, "early-balanced-mobility",
                      "........\n"
                      "W...W...\n"
                      "BWB.W...\n"
                      "..WBW...\n"
                      "..BBBB..\n"
                      "....WB..\n"
                      "........\n"
                      "........\n"
                      "side=W",
                      "early_midgame", {}, "fixed-seed legal playout game 56 ply 11")) {
        return std::nullopt;
    }

    if (!add_position(positions, "early-corner-race",
                      "........\n"
                      "...W...W\n"
                      "....WBBB\n"
                      "...BBW..\n"
                      "...WW...\n"
                      "....W...\n"
                      "........\n"
                      "........\n"
                      "side=B",
                      "early_midgame", "high_mobility,corner_available",
                      "fixed-seed legal playout game 18 ply 8")) {
        return std::nullopt;
    }

    if (!add_position(positions, "early-second-pass",
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BBB..\n"
                      "BBBBB...\n"
                      "....B.B.\n"
                      "....WWWW\n"
                      "....W.B.\n"
                      "side=B",
                      "early_midgame", "low_mobility,pass",
                      "fixed-seed legal playout game 4591 ply 12")) {
        return std::nullopt;
    }

    if (!add_position(positions, "early-tight-mobility",
                      "........\n"
                      ".....B..\n"
                      "...W.B..\n"
                      "...WWB..\n"
                      "...WW...\n"
                      ".BBBW...\n"
                      "........\n"
                      "........\n"
                      "side=B",
                      "early_midgame", "low_mobility", "fixed-seed legal playout game 284 ply 8")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-lopsided-edge",
                      "...B.W.B\n"
                      ".B.B.WW.\n"
                      ".B.BBB.W\n"
                      "BBWBBB..\n"
                      "BBWBBB..\n"
                      "BBWBBB..\n"
                      "BWWB....\n"
                      "BBBBB...\n"
                      "side=W",
                      "midgame", "high_mobility,edge_heavy,score_lopsided",
                      "fixed-seed legal playout game 3723 ply 35")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-white-wall",
                      "WWWWW.W.\n"
                      "WWWWWW..\n"
                      "BWWWWW..\n"
                      "..WBW...\n"
                      ".WWWWW..\n"
                      "W.W.WBBB\n"
                      "..W....B\n"
                      "..W....B\n"
                      "side=B",
                      "midgame", "edge_heavy,score_lopsided",
                      "fixed-seed legal playout game 797 ply 32")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-pass-edge",
                      "WWWWBBBB\n"
                      "WWWBBB..\n"
                      "WWWBBBBB\n"
                      "WW.BBB..\n"
                      "W.BBB.B.\n"
                      "..B....B\n"
                      "........\n"
                      "........\n"
                      "side=B",
                      "midgame", "low_mobility,pass,edge_heavy",
                      "fixed-seed legal playout game 2319 ply 30")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-normal-mobility",
                      "....WBBB\n"
                      "...WWWBW\n"
                      "....WBWW\n"
                      "...WWW.W\n"
                      "..WWW...\n"
                      "..B.W...\n"
                      "....W...\n"
                      "....W...\n"
                      "side=B",
                      "midgame", {}, "fixed-seed legal playout game 18 ply 20")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-x-risk",
                      ".....W..\n"
                      "...WWW..\n"
                      "...WWW..\n"
                      "..WWWWBB\n"
                      "..WWWWBW\n"
                      "..WWWWWW\n"
                      "...WB...\n"
                      "...W....\n"
                      "side=B",
                      "midgame", "high_mobility,x_square_risk,score_lopsided",
                      "fixed-seed legal playout game 702 ply 24")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-balanced-count",
                      "....B.B.\n"
                      "W...BB..\n"
                      "WWB.B..W\n"
                      "W.WBB..W\n"
                      ".WBBWWWW\n"
                      "W.B.WW..\n"
                      ".B...BW.\n"
                      ".....BBB\n"
                      "side=B",
                      "midgame", {}, "fixed-seed legal playout game 56 ply 28")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-wide-x-risk",
                      "....WWBB\n"
                      ".....WB.\n"
                      "...WWWWW\n"
                      "..BBBB..\n"
                      ".WWWB...\n"
                      "..W.....\n"
                      "........\n"
                      "........\n"
                      "side=B",
                      "midgame", "high_mobility,x_square_risk",
                      "fixed-seed legal playout game 37 ply 16")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-low-mobility",
                      "....B...\n"
                      "..B.B...\n"
                      "...BB..B\n"
                      "...WB.B.\n"
                      "..BWWB..\n"
                      "...WB...\n"
                      "...BWB..\n"
                      ".WWWW...\n"
                      "side=B",
                      "midgame", "low_mobility", "fixed-seed legal playout game 1557 ply 18")) {
        return std::nullopt;
    }

    if (!add_position(positions, "late-black-pass",
                      "..WWWWW.\n"
                      "...BBBWB\n"
                      "WBBWWWWW\n"
                      "WBWWWBBB\n"
                      "WWWWB...\n"
                      "WWWWWB..\n"
                      "WWWWWW..\n"
                      "WWWWWWW.\n"
                      "side=B",
                      "late_midgame", "low_mobility,pass,edge_heavy,score_lopsided",
                      "fixed-seed legal playout game 4196 ply 46")) {
        return std::nullopt;
    }

    if (!add_position(positions, "late-corner-swing",
                      "........\n"
                      ".B.B.B..\n"
                      "..BWWBB.\n"
                      ".BWBWBB.\n"
                      "BBBWBW.B\n"
                      "BBBBWWWB\n"
                      "BBBB..WW\n"
                      "BBBBBBBW\n"
                      "side=W",
                      "late_midgame",
                      "high_mobility,corner_available,edge_heavy,x_square_risk,score_lopsided",
                      "fixed-seed legal playout game 288 ply 39")) {
        return std::nullopt;
    }

    if (!add_position(positions, "late-edge-heavy",
                      "..W..B.B\n"
                      "B..W.BB.\n"
                      ".B.BBBWW\n"
                      "BBBBBBWB\n"
                      ".BWWWB..\n"
                      ".WWWWWBW\n"
                      "BBBBBB.B\n"
                      "BBBBBBB.\n"
                      "side=W",
                      "late_midgame",
                      "high_mobility,corner_available,edge_heavy,x_square_risk,score_lopsided",
                      "fixed-seed legal playout game 339 ply 43")) {
        return std::nullopt;
    }

    if (!add_position(positions, "late-open-corner",
                      "...BBB.W\n"
                      "...BB.W.\n"
                      "BBBBBB.B\n"
                      ".B.BWBB.\n"
                      "..BWWWBW\n"
                      ".BBBBBBB\n"
                      ".B.W.BBW\n"
                      "WBBB....\n"
                      "side=W",
                      "late_midgame",
                      "high_mobility,corner_available,edge_heavy,x_square_risk,score_lopsided",
                      "fixed-seed legal playout game 1189 ply 37")) {
        return std::nullopt;
    }

    if (!add_position(positions, "late-wide-mobility",
                      "..BBBBW.\n"
                      "..BBBBBB\n"
                      ".BBBWBBB\n"
                      ".BBWWBBB\n"
                      ".BWWBBB.\n"
                      "WWWWB.B.\n"
                      "WB..B.BW\n"
                      "WB..B...\n"
                      "side=W",
                      "late_midgame",
                      "high_mobility,corner_available,edge_heavy,x_square_risk,score_lopsided",
                      "fixed-seed legal playout game 1512 ply 41")) {
        return std::nullopt;
    }

    if (!add_position(positions, "endgame-pass-two-empty",
                      "WWBBBBBB\n"
                      ".BBWWBBW\n"
                      "B.BWWBWW\n"
                      "BBBWBWBB\n"
                      "BBBBWBBB\n"
                      "BWBBBBWB\n"
                      "BBBBBBBB\n"
                      "BWBBBBBB\n"
                      "side=B",
                      "endgame-ish", "low_mobility,pass,edge_heavy,score_lopsided,dense_late_game",
                      "fixed-seed legal playout game 118 ply 58")) {
        return std::nullopt;
    }

    if (!add_position(
            positions, "endgame-corner-choice",
            ".WWWWWB.\n"
            "..WWWB..\n"
            "WWWWBBB.\n"
            "W.WWBBWB\n"
            "WBWWWWWW\n"
            "WBWWWWWW\n"
            "WBBWWWWW\n"
            "WWWWBWWW\n"
            "side=B",
            "endgame-ish",
            "low_mobility,corner_available,edge_heavy,x_square_risk,score_lopsided,dense_late_game",
            "fixed-seed legal playout game 101 ply 52")) {
        return std::nullopt;
    }

    if (!add_position(positions, "endgame-dense-mobility",
                      "...B....\n"
                      "WWBWWW.B\n"
                      "WWWWWWWW\n"
                      "WBWWWWW.\n"
                      "WWWBBWBB\n"
                      "WBBBWB.B\n"
                      "WWBWBWBB\n"
                      "WWWWWWWB\n"
                      "side=B",
                      "endgame-ish",
                      "high_mobility,corner_available,edge_heavy,x_square_risk,score_lopsided,"
                      "dense_late_game",
                      "fixed-seed legal playout game 3904 ply 50")) {
        return std::nullopt;
    }

    return positions;
}

[[nodiscard]] inline std::optional<std::vector<Position>> make_search_evaluation_positions() {
    std::vector<Position> positions;
    positions.reserve(8);

    if (!add_position(positions, "eval-corner-access-a1",
                      "........\n"
                      ".B......\n"
                      "..B.B...\n"
                      "...BB...\n"
                      ".WWWWW..\n"
                      "..B.....\n"
                      "........\n"
                      "........\n"
                      "side=W",
                      "opening",
                      "corner_access,corner_available,opening,x_square_danger,x_square_risk",
                      "curated evaluation diagnostic: tactical corner access near A1")) {
        return std::nullopt;
    }

    if (!add_position(positions, "eval-x-square-danger",
                      ".....W..\n"
                      "...WWW..\n"
                      "...WWW..\n"
                      "..WWWWBB\n"
                      "..WWWWBW\n"
                      "..WWWWWW\n"
                      "...WB...\n"
                      "...W....\n"
                      "side=B",
                      "midgame", "x_square_danger,x_square_risk,midgame,high_mobility,score_lopsided",
                      "curated evaluation diagnostic: X-square danger and local corner shape")) {
        return std::nullopt;
    }

    if (!add_position(positions, "eval-mobility-pressure",
                      "WWWWBBBB\n"
                      "WWWBBB..\n"
                      "WWWBBBBB\n"
                      "WW.BBB..\n"
                      "W.BBB.B.\n"
                      "..B....B\n"
                      "........\n"
                      "........\n"
                      "side=B",
                      "midgame", "mobility_pressure,low_mobility,pass,edge_heavy",
                      "curated evaluation diagnostic: low-mobility edge pressure")) {
        return std::nullopt;
    }

    if (!add_position(positions, "eval-frontier-heavy-ring",
                      "........\n"
                      "........\n"
                      "..WWW...\n"
                      "..WBW...\n"
                      "..WWW...\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B",
                      "midgame", "frontier_heavy,midgame,x_square_risk",
                      "curated evaluation diagnostic: compact frontier-heavy ring")) {
        return std::nullopt;
    }

    if (!add_position(positions, "eval-edge-pattern-lopsided",
                      "...B.W.B\n"
                      ".B.B.WW.\n"
                      ".B.BBB.W\n"
                      "BBWBBB..\n"
                      "BBWBBB..\n"
                      "BBWBBB..\n"
                      "BWWB....\n"
                      "BBBBB...\n"
                      "side=W",
                      "midgame", "edge_pattern,edge_heavy,score_lopsided,midgame,high_mobility",
                      "curated evaluation diagnostic: lopsided edge shape")) {
        return std::nullopt;
    }

    if (!add_position(positions, "eval-edge-corner-race",
                      "........\n"
                      ".B.B.B..\n"
                      "..BWWBB.\n"
                      ".BWBWBB.\n"
                      "BBBWBW.B\n"
                      "BBBBWWWB\n"
                      "BBBB..WW\n"
                      "BBBBBBBW\n"
                      "side=W",
                      "late_midgame",
                      "edge_pattern,corner_access,corner_available,edge_heavy,late_midgame,"
                      "high_mobility,x_square_risk,score_lopsided",
                      "curated evaluation diagnostic: late edge and corner race")) {
        return std::nullopt;
    }

    if (!add_position(positions, "eval-late-open-corner",
                      "...BBB.W\n"
                      "...BB.W.\n"
                      "BBBBBB.B\n"
                      ".B.BWBB.\n"
                      "..BWWWBW\n"
                      ".BBBBBBB\n"
                      ".B.W.BBW\n"
                      "WBBB....\n"
                      "side=W",
                      "late_midgame",
                      "late_pre_endgame,corner_access,corner_available,edge_heavy,x_square_danger,"
                      "x_square_risk,high_mobility,score_lopsided",
                      "curated evaluation diagnostic: late-pre-endgame open-corner shape")) {
        return std::nullopt;
    }

    if (!add_position(positions, "eval-late-dense-mobility",
                      "...B....\n"
                      "WWBWWW.B\n"
                      "WWWWWWWW\n"
                      "WBWWWWW.\n"
                      "WWWBBWBB\n"
                      "WBBBWB.B\n"
                      "WWBWBWBB\n"
                      "WWWWWWWB\n"
                      "side=B",
                      "endgame-ish",
                      "late_pre_endgame,high_mobility,corner_available,edge_heavy,dense_late_game,"
                      "x_square_risk,score_lopsided",
                      "curated evaluation diagnostic: dense late-pre-endgame mobility")) {
        return std::nullopt;
    }

    return positions;
}

[[nodiscard]] inline std::optional<std::vector<Position>> make_search_threshold_positions() {
    auto positions = make_search_suite_positions();
    if (!positions.has_value()) {
        return std::nullopt;
    }
    positions->reserve(positions->size() + 12);

    if (!add_position(*positions, "threshold-15-gated-exact",
                      "...W.BBW\n"
                      "....BBBB\n"
                      "WB.BBWBB\n"
                      ".BBBWWBB\n"
                      ".BWBWBBB\n"
                      ".BWWWBBB\n"
                      ".BWWW.BB\n"
                      "BBBBW.WB\n"
                      "side=W",
                      "threshold_endgame", "experimental_15,high_mobility,edge_heavy",
                      "15-empty exact threshold fixture; expected adaptive16 exact root at "
                      "legal_moves_current <= 10")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-15-gated-skip",
                      "BBB..W.B\n"
                      "BWWBB.B.\n"
                      ".W.BBBWW\n"
                      "WWBWBBWW\n"
                      "..WWWBWW\n"
                      ".BBWWWWW\n"
                      "BBBBBBBB\n"
                      "..B.W.W.\n"
                      "side=W",
                      "threshold_endgame",
                      "experimental_15,high_mobility,corner_available,edge_heavy",
                      "15-empty exact threshold fixture; expected adaptive16 skip for "
                      "legal_moves_current")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-15-low-branching",
                      "...WBW..\n"
                      "..WWB.WW\n"
                      ".WWWBWW.\n"
                      ".WBBBBB.\n"
                      "WBWBBBBW\n"
                      "BBBBBWWW\n"
                      "BBBBWWWW\n"
                      "BW.B.WW.\n"
                      "side=W",
                      "threshold_endgame", "experimental_15,low_mobility,edge_heavy",
                      "15-empty exact threshold fixture; expected adaptive16 skip for "
                      "legal_moves_opponent")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-15-parity-ish",
                      "...WB...\n"
                      "....B..B\n"
                      "BBBBBWBB\n"
                      ".BBBBBWB\n"
                      "BWBWBWBB\n"
                      "WWWBWBB.\n"
                      "WWWWWWB.\n"
                      "WWWWWWWB\n"
                      "side=W",
                      "threshold_endgame",
                      "experimental_15,high_mobility,edge_heavy,x_square_risk",
                      "15-empty exact threshold fixture; expected adaptive16 exact root at "
                      "legal_moves_current <= 10")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-16-low-mobility",
                      "...W.B.W\n"
                      "....BBWB\n"
                      "WB.BBWBB\n"
                      ".BBBWWBB\n"
                      ".BWBWBBB\n"
                      ".BWWWBBB\n"
                      ".BWWW.BB\n"
                      "BBBBW.WB\n"
                      "side=B",
                      "threshold_endgame", "experimental_16,low_mobility,edge_heavy",
                      "16-empty exact threshold fixture; expected adaptive16 skip for "
                      "legal_moves_opponent")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-16-normal-mobility",
                      "BBB..W.B\n"
                      "BWWB..B.\n"
                      ".W.WWBWW\n"
                      "WWBWBBWW\n"
                      "..WWWBWW\n"
                      ".BBWWWWW\n"
                      "BBBBBBBB\n"
                      "..B.W.W.\n"
                      "side=B",
                      "threshold_endgame",
                      "experimental_16,normal_mobility,edge_heavy,balanced_count",
                      "16-empty exact threshold fixture; expected adaptive16 skip for "
                      "legal_moves_opponent")) {
        return std::nullopt;
    }

    if (!add_position(
            *positions, "threshold-16-high-mobility",
            "...W.W..\n"
            "..WWW.WW\n"
            ".WWWBWW.\n"
            ".WBBBBB.\n"
            "WBWBBBBW\n"
            "BBBBBWWW\n"
            "BBBBWWWW\n"
            "BW.B.WW.\n"
            "side=B",
            "threshold_endgame",
            "experimental_16,high_mobility,corner_available,corner_choice,edge_heavy,"
            "balanced_count,x_square_risk",
            "16-empty exact threshold fixture; expected adaptive16 skip for legal_moves_current")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-16-root-pass",
                      "........\n"
                      "BBBBBBBW\n"
                      "BBWWBBB.\n"
                      "BBBBBBBB\n"
                      ".BBBBWBB\n"
                      "..BBBWBB\n"
                      "..BBBWWW\n"
                      "..BBBBBW\n"
                      "side=B",
                      "threshold_endgame",
                      "experimental_16,pass,low_mobility,edge_heavy,score_lopsided",
                      "16-empty exact threshold fixture; expected adaptive16 skip for root pass")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-16-corner-choice",
                      ".WB.B.BW\n"
                      ".WWWWWWW\n"
                      ".WBWWWWB\n"
                      "W.WBWWW.\n"
                      "WWBBBW.W\n"
                      "WBBBB.W.\n"
                      "WBBBBB..\n"
                      "WBW...B.\n"
                      "side=B",
                      "threshold_endgame",
                      "experimental_16,corner_choice,corner_available,edge_heavy,high_mobility",
                      "16-empty exact threshold fixture; expected adaptive16 exact root")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-16-corner-race",
                      ".BBBB...\n"
                      "..WWWB..\n"
                      ".WWWWW..\n"
                      "WWWBWWWW\n"
                      "WWWBWWWW\n"
                      "W.BWWWWW\n"
                      "WBBBWWW.\n"
                      "B.WWWW..\n"
                      "side=B",
                      "threshold_endgame",
                      "experimental_16,corner_race,corner_available,edge_heavy,high_mobility,"
                      "x_square_risk,score_lopsided",
                      "16-empty exact threshold fixture; expected adaptive16 exact root")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-16-edge-heavy",
                      ".B....BW\n"
                      ".WWWWWW.\n"
                      ".WWWWBWW\n"
                      ".WBWWWWW\n"
                      "WWBWWBWW\n"
                      ".WWWBBWW\n"
                      "WWBBBBW.\n"
                      "W...B.W.\n"
                      "side=B",
                      "threshold_endgame",
                      "experimental_16,corner_available,high_mobility,score_lopsided",
                      "16-empty exact threshold fixture; expected adaptive16 skip for "
                      "legal_moves_current")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "threshold-16-parity-ish",
                      "...W....\n"
                      "....W..B\n"
                      "BBBBBWBB\n"
                      ".BBBBBWB\n"
                      "BWBWBWBB\n"
                      "WWWBWBB.\n"
                      "WWWWWWB.\n"
                      "WWWWWWWB\n"
                      "side=B",
                      "threshold_endgame",
                      "experimental_16,parity-ish,balanced_count,edge_heavy,x_square_risk",
                      "16-empty exact threshold fixture; expected adaptive16 exact root")) {
        return std::nullopt;
    }

    return positions;
}

} // namespace othello::benchmarks
