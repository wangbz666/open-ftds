/* SPDX-License-Identifier: MIT */
#ifndef OPENFTDS_OPENFTDS_HPP
#define OPENFTDS_OPENFTDS_HPP

#include "openftds.h"

namespace openftds {

class ScopedDelay {
public:
    ScopedDelay(const char *name, const char *module, uint32_t event_id,
                openftds_level_t level = OPENFTDS_LEVEL_NORMAL) noexcept
        : name_(name), module_(module), event_id_(event_id), level_(level),
          token_(openftds_delay_start(level)), status_(0) {}

    ~ScopedDelay() noexcept {
        openftds_delay_end(name_, module_, event_id_, level_, token_, status_);
    }

    ScopedDelay(const ScopedDelay &) = delete;
    ScopedDelay &operator=(const ScopedDelay &) = delete;

    void set_status(int status) noexcept { status_ = status; }

private:
    const char *name_;
    const char *module_;
    uint32_t event_id_;
    openftds_level_t level_;
    openftds_token_t token_;
    int status_;
};

} // namespace openftds

#define OPENFTDS_CONCAT_INNER(a, b) a##b
#define OPENFTDS_CONCAT(a, b) OPENFTDS_CONCAT_INNER(a, b)
#define OPENFTDS_SCOPED_DELAY(NAME, MODULE, ID, LEVEL) \
    ::openftds::ScopedDelay OPENFTDS_CONCAT(openftds_scope_, __LINE__)((NAME), (MODULE), (ID), (LEVEL))

#endif
