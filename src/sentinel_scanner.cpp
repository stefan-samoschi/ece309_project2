#include "core/sentinel_scanner.h"

#include <stdexcept>
#include <utility>

SentinelScanner::SentinelScanner(std::string sentinel)
    : sentinel_(std::move(sentinel)) {
    if (sentinel_.empty()) {
        throw std::invalid_argument("empty sentinel");
    }
}

SentinelScanner::Out SentinelScanner::feed(std::string_view chunk) {
    if (found_) {
        return {"", true};
    }

    Out result{"", false};

    for (char ch : chunk) {
        pending_ += ch;

        // Release leading characters until pending could still be the start of the sentinel.
        while (!pending_.empty() &&
               sentinel_.compare(0, pending_.size(), pending_) != 0) {
            result.safe_text += pending_[0];
            pending_.erase(0, 1);
        }

        if (pending_.size() == sentinel_.size()) {
            pending_.clear();
            found_ = true;
            result.sentinel_found = true;
            break;
        }
    }

    return result;
}

SentinelScanner::Out SentinelScanner::flush() {
    if (found_) {
        return {"", true};
    }

    std::string safe = std::move(pending_);
    pending_.clear();
    return {std::move(safe), false};
}