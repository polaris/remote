#ifndef RPC_APPLY_TUPLE_FROM_H
#define RPC_APPLY_TUPLE_FROM_H

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace remote {

namespace detail {

template<typename F, typename Tuple, size_t ...S>
decltype(auto) apply_tuple_impl(F &&fn, Tuple &&t, std::index_sequence<S...>) {
    return std::forward<F>(fn)(std::get<S>(std::forward<Tuple>(t))...);
}

template<typename F, typename Tuple>
decltype(auto) apply_from_tuple(F &&fn, Tuple &&t) {
    std::size_t constexpr size = std::tuple_size<typename std::remove_reference<Tuple>::type>::value;
    return apply_tuple_impl(std::forward<F>(fn), std::forward<Tuple>(t), std::make_index_sequence<size>());
}

}   // namespace detail

}   // namespace remote

#endif //RPC_APPLY_TUPLE_FROM_H
