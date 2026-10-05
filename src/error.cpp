#include <remote/error.h>

#include <string>

namespace remote {

namespace {

class remote_error_category final : public boost::system::error_category {
public:
    const char *name() const noexcept override {
        return "remote";
    }

    std::string message(int value) const override {
        switch (static_cast<error>(value)) {
            case error::not_connected:
                return "not connected";
            case error::protocol_error:
                return "protocol error";
            case error::message_too_large:
                return "message too large";
            case error::unknown_procedure:
                return "unknown procedure";
            case error::invalid_arguments:
                return "invalid arguments";
            case error::procedure_failed:
                return "procedure failed";
            case error::invalid_result:
                return "invalid result";
            case error::timed_out:
                return "timed out";
        }
        return "unknown error";
    }
};

}   // namespace

const boost::system::error_category &error_category() noexcept {
    static const remote_error_category category;
    return category;
}

}   // namespace remote
