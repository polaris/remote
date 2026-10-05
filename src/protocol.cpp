#include <remote/detail/protocol.h>

#include <boost/system/system_error.hpp>

#include <sstream>

namespace remote::detail {

namespace {

[[noreturn]] void throw_protocol_error(const char *what) {
    throw boost::system::system_error{error::protocol_error, what};
}

bool is_unsigned_integer(const msgpack::object &obj) {
    return obj.type == msgpack::type::POSITIVE_INTEGER;
}

bool is_string(const msgpack::object &obj) {
    return obj.type == msgpack::type::STR;
}

bool is_array(const msgpack::object &obj) {
    return obj.type == msgpack::type::ARRAY;
}

std::uint32_t parse_msgid(const msgpack::object &obj) {
    if (!is_unsigned_integer(obj) || obj.via.u64 > UINT32_MAX) {
        throw_protocol_error("invalid message id");
    }
    return static_cast<std::uint32_t>(obj.via.u64);
}

std::string parse_method(const msgpack::object &obj) {
    if (!is_string(obj)) {
        throw_protocol_error("method name is not a string");
    }
    return obj.as<std::string>();
}

msgpack::object parse_params(const msgpack::object &obj) {
    if (!is_array(obj)) {
        throw_protocol_error("params is not an array");
    }
    return obj;
}

}   // namespace

message parse_message(const msgpack::object &obj) {
    if (!is_array(obj) || obj.via.array.size == 0 || !is_unsigned_integer(obj.via.array.ptr[0])) {
        throw_protocol_error("message is not a msgpack-rpc array");
    }
    const msgpack::object *fields = obj.via.array.ptr;
    const auto size = obj.via.array.size;
    switch (fields[0].via.u64) {
        case static_cast<int>(message_type::request):
            if (size != 4) {
                throw_protocol_error("request must have 4 elements");
            }
            return request{.msgid = parse_msgid(fields[1]),
                           .method = parse_method(fields[2]),
                           .params = parse_params(fields[3])};
        case static_cast<int>(message_type::response):
            if (size != 4) {
                throw_protocol_error("response must have 4 elements");
            }
            return response{.msgid = parse_msgid(fields[1]), .error = fields[2], .result = fields[3]};
        case static_cast<int>(message_type::notification):
            if (size != 3) {
                throw_protocol_error("notification must have 3 elements");
            }
            return notification{.method = parse_method(fields[1]), .params = parse_params(fields[2])};
        default:
            throw_protocol_error("unknown message type");
    }
}

msgpack::sbuffer pack_request(std::uint32_t msgid, std::string_view method, const msgpack::sbuffer &params) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer{buffer};
    packer.pack_array(4);
    packer.pack(static_cast<int>(message_type::request));
    packer.pack(msgid);
    packer.pack(method);
    buffer.write(params.data(), params.size());
    return buffer;
}

msgpack::sbuffer pack_notification(std::string_view method, const msgpack::sbuffer &params) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer{buffer};
    packer.pack_array(3);
    packer.pack(static_cast<int>(message_type::notification));
    packer.pack(method);
    buffer.write(params.data(), params.size());
    return buffer;
}

msgpack::sbuffer pack_result(std::uint32_t msgid, const msgpack::sbuffer &result) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer{buffer};
    packer.pack_array(4);
    packer.pack(static_cast<int>(message_type::response));
    packer.pack(msgid);
    packer.pack_nil();
    buffer.write(result.data(), result.size());
    return buffer;
}

msgpack::sbuffer pack_error(std::uint32_t msgid, error code, std::string_view text) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer{buffer};
    packer.pack_array(4);
    packer.pack(static_cast<int>(message_type::response));
    packer.pack(msgid);
    packer.pack_array(2);
    packer.pack(static_cast<int>(code));
    packer.pack(text);
    packer.pack_nil();
    return buffer;
}

std::exception_ptr make_exception(const msgpack::object &error_obj) {
    // Errors produced by remote are [code, message]. Other msgpack-rpc implementations commonly
    // send a plain string; anything else is reported in its textual form.
    if (is_array(error_obj) && error_obj.via.array.size == 2
        && is_unsigned_integer(error_obj.via.array.ptr[0]) && is_string(error_obj.via.array.ptr[1])) {
        const auto code = error_obj.via.array.ptr[0].via.u64;
        auto text = error_obj.via.array.ptr[1].as<std::string>();
        switch (code) {
            case static_cast<int>(error::unknown_procedure):
            case static_cast<int>(error::invalid_arguments):
            case static_cast<int>(error::procedure_failed):
                return std::make_exception_ptr(
                        boost::system::system_error{static_cast<error>(code), text});
            default:
                break;
        }
        return std::make_exception_ptr(boost::system::system_error{error::procedure_failed, text});
    }
    if (is_string(error_obj)) {
        return std::make_exception_ptr(
                boost::system::system_error{error::procedure_failed, error_obj.as<std::string>()});
    }
    std::ostringstream text;
    text << error_obj;
    return std::make_exception_ptr(boost::system::system_error{error::procedure_failed, text.str()});
}

}   // namespace remote::detail
