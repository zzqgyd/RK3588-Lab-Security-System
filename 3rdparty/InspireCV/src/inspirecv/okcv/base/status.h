
#ifndef OKCV_STATUS_H_
#define OKCV_STATUS_H_

#include <memory>
#include <string>

namespace okcv {

namespace error {

/**
 * @brief Enumeration for different error codes.
 */
enum Code {
    // Success
    OK = 0,

    // Unknown.
    UNKNOWN = 1,

    // Use invalid argument.
    INVALID_ARGUMENT = 2,

    // Not found file or directory.
    NOT_FOUND = 5,

    // Already exists file or directory.
    ALREADY_EXISTS = 6,

    // Not implemented.
    UNIMPLEMENTED = 12,
};

}  // namespace error

/**
 * @brief Represents the status of an operation.
 */
class Status {
public:
    Status() {}

    explicit Status(error::Code code, const std::string &message = "") {
        state_ = std::unique_ptr<State>(new State);
        state_->code = code;
        state_->message = message;
    }

    inline Status(const Status &s)
    : state_((s.state_ == nullptr) ? nullptr : new State(*s.state_)) {}

    void operator=(const Status &s) {
        if (s.state_ == nullptr) {
            state_ = nullptr;
        } else {
            state_ = std::unique_ptr<State>(new State(*s.state_));
        }
    }

    bool ok() const {
        return state_ == nullptr;
    }

    operator bool() const {
        return ok();
    }

    static Status OK() {
        return Status();
    }

    std::string message() const {
        return ok() ? "" : state_->message;
    }

private:
    struct State {
        error::Code code;
        std::string message;
    };

    std::unique_ptr<State> state_;
};

}  // namespace okcv

#endif  // OKCV_STATUS_H_
