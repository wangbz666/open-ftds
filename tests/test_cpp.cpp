/* SPDX-License-Identifier: MIT */
#include "openftds/openftds.hpp"

#include <type_traits>

int main() {
    static_assert(OPENFTDS_VERSION_MAJOR == 0);
    static_assert(OPENFTDS_VERSION_MINOR == 2);
    static_assert(!std::is_copy_constructible_v<openftds::ScopedDelay>);
    openftds_status_t status{};
    return openftds_get_status(&status);
}
