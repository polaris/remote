#ifndef REMOTE_EXAMPLES_KV_API_H
#define REMOTE_EXAMPLES_KV_API_H

#include <remote/procedure.h>

#include <optional>
#include <string>
#include <vector>

// The interface of the key-value store, shared by kv_server and kv_client.
namespace kv {

inline constexpr remote::procedure<void(std::string, std::string)> put{"kv.put"};
inline constexpr remote::procedure<std::optional<std::string>(std::string)> get{"kv.get"};
inline constexpr remote::procedure<bool(std::string)> erase{"kv.erase"};
inline constexpr remote::procedure<std::vector<std::string>()> keys{"kv.keys"};

}   // namespace kv

#endif //REMOTE_EXAMPLES_KV_API_H
