//
// Created by root on 11/29/25.
//

#ifndef RPCSERVER_H
#define RPCSERVER_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>
#include <span>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>
#include <uvent/net/Socket.h>

#include <ulog/ulog.h>

#include <urpc/config/Config.h>
#include <urpc/registry/RPCMethodRegistry.h>
#include <urpc/router/RPCRouter.h>
#include <urpc/connection/RPCConnection.h>
#include <urpc/transport/IRPCStreamFactory.h>
#include <urpc/context/RPCContext.h>

namespace urpc
{
    class RpcServer
    {
    public:
        RpcServer(std::string host,
                  uint16_t port,
                  int threads);

        explicit RpcServer(RpcServerConfig cfg);

        RpcMethodRegistry& registry();

        // The server's primary dispatch router. Handlers registered via the
        // register_method* helpers and any mounted routers land here.
        RpcRouter& router();

        template <uint64_t MethodId, typename F>
        void register_method_ct(F&& f)
        {
#if URPC_LOGS
            usub::ulog::debug(
                "RpcServer: register_method_ct MethodId={}",
                MethodId);
#endif
            this->router_.on_ct<MethodId>(std::forward<F>(f));
        }

        void register_method(uint64_t method_id, RpcHandlerPtr fn);
        void register_method(std::string_view name, RpcHandlerPtr fn);

        // Mount an externally-built router. Every route it carries (and its
        // not-found handler, if the server's router has none) is merged into
        // the server's dispatch router. Build routers in separate translation
        // units and attach them here instead of binding each method directly.
        void mount(const RpcRouter& router);

        usub::uvent::task::Awaitable<void> run_async();
        void run();

    private:
        usub::uvent::task::Awaitable<void> accept_loop();

    private:
        RpcMethodRegistry registry_;
        RpcRouter         router_;
        RpcServerConfig   config_;
    };
}

#endif // RPCSERVER_H