#include "protocols/nboard/protocol.hpp"

#include "protocols/nboard/game_codec.hpp"

#include <sstream>

namespace othello::tools::nboard {

std::string nboard_command(int version) {
    return "nboard " + std::to_string(version);
}

std::string set_depth_command(int depth) {
    return "set depth " + std::to_string(depth);
}

std::string set_game_command(const std::vector<std::string>& moves) {
    return format_set_game_command(moves);
}

std::string ping_command(int id) {
    return "ping " + std::to_string(id);
}

std::string go_command() {
    return "go";
}

std::string quit_command() {
    return "quit";
}

std::string move_command(std::string_view move) {
    std::ostringstream output;
    output << "move " << move;
    return output.str();
}

} // namespace othello::tools::nboard
