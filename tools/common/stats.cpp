#include "common/stats.hpp"

namespace othello::tools {

double rate(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    if (denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

} // namespace othello::tools
