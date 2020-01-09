#ifndef REMOTE_LOG_ENTRIES_H
#define REMOTE_LOG_ENTRIES_H

#include <cstdint>
#include <string>
#include <vector>

#include <msgpack.hpp>

#define REMOTE_DEFINE MSGPACK_DEFINE_ARRAY

struct log_entry {
    uint32_t term;
    uint32_t index;
    std::string command;
    REMOTE_DEFINE(term, index, command);
};
using log_entry_vector = std::vector<log_entry>;

#endif //REMOTE_LOG_ENTRIES_H
