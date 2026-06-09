//
// RpcRouter — composable request router for uRPC.
//
// A router is a standalone, independently-built collection of method handlers.
// Instead of binding every handler directly onto an RpcServer, you can build
// one or more routers (each optionally namespaced with a prefix), compose them
// with merge(), and then mount() the result onto a server. This decouples
// handler definition from the server instance: handlers live wherever you
// build the router (a separate translation unit, a feature module, a plugin),
// and the server only learns about them when the router is mounted.
//
// Unlike RpcMethodRegistry, a router stores type-erased RpcHandler objects, so
// handlers may capture state (closures, shared services, middleware wrappers).
//

#ifndef RPCROUTER_H
#define RPCROUTER_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <urpc/context/RPCContext.h>
#include <urpc/utils/Hash.h>

namespace urpc
{
    class RpcMethodRegistry;

    // Wrap an arbitrary handler functor into a type-erased RpcHandler.
    //
    // Mirrors RpcServer::register_method_ct semantics: the functor may return
    //   * Awaitable<std::vector<uint8_t>>  -> forwarded as-is,
    //   * Awaitable<R> for any ByteRange R -> re-encoded to bytes (e.g. string),
    //   * Awaitable<void>                  -> empty response.
    template <typename F>
    RpcHandler make_handler(F&& f)
    {
        using Functor = std::decay_t<F>;
        using RawRet = std::invoke_result_t<
            Functor&,
            RpcContext&,
            std::span<const std::uint8_t>>;
        using Result = detail::awaitable_value_t<RawRet>;

        // Store the functor behind a shared_ptr so the resulting RpcHandler
        // stays copyable and keeps the captured state alive for the lifetime
        // of every route that references it.
        auto holder = std::make_shared<Functor>(std::forward<F>(f));

        return [holder](RpcContext& ctx,
                        std::span<const std::uint8_t> body)
            -> usub::uvent::task::Awaitable<std::vector<std::uint8_t>>
        {
            if constexpr (std::is_same_v<Result, std::vector<std::uint8_t>>)
            {
                co_return co_await (*holder)(ctx, body);
            }
            else if constexpr (ByteRange<Result>)
            {
                Result r = co_await (*holder)(ctx, body);
                co_return to_byte_vector(std::move(r));
            }
            else
            {
                static_assert(
                    std::is_same_v<Result, void>,
                    "make_handler: unsupported handler result type");
                co_return std::vector<std::uint8_t>{};
            }
        };
    }

    // A resolved, non-owning view of a handler. When valid, exactly one of
    // fn (a type-erased router handler) or raw (a legacy registry function
    // pointer) is set. Both null means "no handler".
    struct RpcRoute
    {
        const RpcHandler* fn{nullptr};
        RpcHandlerPtr     raw{nullptr};

        explicit operator bool() const noexcept
        {
            return this->fn != nullptr || this->raw != nullptr;
        }
    };

    class RpcRouter
    {
    public:
        RpcRouter() = default;

        // Build a namespaced router. The prefix is prepended to every name
        // passed to the string-based registration helpers before it is hashed,
        // so on("Get", ...) on a router with prefix "User." registers the
        // method id of "User.Get".
        explicit RpcRouter(std::string prefix);

        // ---- runtime registration (stateful handlers allowed) ----
        RpcRouter& on(uint64_t method_id, RpcHandler handler);
        RpcRouter& on(std::string_view name, RpcHandler handler);

        // ---- compile-time method id, with automatic result wrapping ----
        template <uint64_t MethodId, typename F>
        RpcRouter& on_ct(F&& f)
        {
            return this->on(MethodId, make_handler(std::forward<F>(f)));
        }

        // ---- name-based registration, with automatic result wrapping ----
        // The router prefix (if any) is applied to the name.
        template <typename F>
        RpcRouter& route(std::string_view name, F&& f)
        {
            return this->on(name, make_handler(std::forward<F>(f)));
        }

        // ---- composition ----
        // Copy every route from `other` into this router. On a method-id
        // collision the entry from `other` wins. If this router has no
        // not-found handler yet, it adopts `other`'s.
        RpcRouter& merge(const RpcRouter& other);

        // ---- catch-all for unknown method ids ----
        // Like route(), the functor's result is wrapped automatically, so the
        // fallback may return vector<uint8_t>, any ByteRange (e.g. string), or
        // void.
        template <typename F>
        RpcRouter& fallback(F&& f)
        {
            this->not_found_ = make_handler(std::forward<F>(f));
            return *this;
        }

        // ---- legacy registry chaining ----
        // A non-owning fallback registry consulted after this router's own map
        // (used by RpcServer so handlers registered via registry() still
        // resolve). The registry must outlive the router.
        void set_fallback_registry(const RpcMethodRegistry* registry) noexcept;

        // ---- lookup ----
        const RpcHandler* find(uint64_t method_id) const;

        // Full resolution: own map -> fallback registry -> not-found handler.
        // Returns an empty RpcRoute only when nothing matches and no fallback
        // handler is configured.
        RpcRoute resolve(uint64_t method_id) const;

        bool has_fallback() const noexcept
        {
            return static_cast<bool>(this->not_found_);
        }

        std::size_t size() const noexcept { return this->handlers_.size(); }

        const std::string& prefix() const noexcept { return this->prefix_; }

    private:
        uint64_t key_for(std::string_view name) const;

    private:
        std::string prefix_;
        std::unordered_map<uint64_t, RpcHandler> handlers_;
        RpcHandler not_found_;
        const RpcMethodRegistry* fallback_registry_{nullptr};
    };
}

#endif // RPCROUTER_H
