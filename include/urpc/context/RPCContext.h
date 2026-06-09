//
// Created by root on 11/29/25.
//

#ifndef RPCCONTEXT_H
#define RPCCONTEXT_H

#include <span>
#include <functional>
#include <concepts>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <vector>
#include <cstring>

#include <uvent/tasks/Awaitable.h>
#include <uvent/sync/AsyncCancellation.h>

#include <urpc/transport/IRPCStream.h>
#include <urpc/transport/TlsPeer.h>

namespace urpc
{
    template <class T>
    concept ByteRange =
        requires(const T& t)
        {
            { t.data() } -> std::convertible_to<const void*>;
            { t.size() } -> std::convertible_to<std::size_t>;
        };

    template <ByteRange R>
    inline std::vector<std::uint8_t> to_byte_vector(R&& r)
    {
        using T = std::remove_cvref_t<R>;
        if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>)
        {
            return std::forward<R>(r);
        }
        else
        {
            const auto sz = static_cast<std::size_t>(r.size());
            std::vector<std::uint8_t> out(sz);
            if (sz != 0)
            {
                std::memcpy(
                    out.data(),
                    static_cast<const void*>(r.data()),
                    sz);
            }
            return out;
        }
    }

    struct RpcContext
    {
        IRpcStream& stream;
        uint32_t stream_id;
        uint64_t method_id;
        uint16_t flags;
        usub::uvent::sync::CancellationToken cancel_token;
        const RpcPeerIdentity* peer{nullptr};
    };

    using RpcHandlerAwaitable = usub::uvent::task::Awaitable<void>;

    using RpcHandlerFn = usub::uvent::task::Awaitable<std::vector<uint8_t>>(
        RpcContext&, std::span<const uint8_t>);

    using RpcHandlerPtr = RpcHandlerFn*;

    // Type-erased, copyable handler. Unlike RpcHandlerPtr (a plain function
    // pointer), this can carry captured state, so routers may hold stateful
    // handlers, closures, and middleware wrappers.
    using RpcHandler = std::function<RpcHandlerFn>;

    namespace detail
    {
        template <class T>
        struct awaitable_value;

        template <class T>
        struct awaitable_value<usub::uvent::task::Awaitable<T>>
        {
            using type = T;
        };

        template <class T>
        using awaitable_value_t = typename awaitable_value<T>::type;
    }
}

#endif // RPCCONTEXT_H
