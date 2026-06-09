//
// main_router.cpp
//
// Demonstrates urpc::RpcRouter: build groups of handlers as standalone,
// independently-constructed objects (here in separate factory functions that
// stand in for separate modules / translation units), compose them, and mount
// them onto the server instead of binding every method directly.
//
//   cmake --build build -j --target urpc_example_router
//   ./build/urpc_example_router
//
// Then drive it with the CLI, e.g.:
//   urpc_cli --host 127.0.0.1 --port 45902 --method User.Greet  --data "kirill"
//   urpc_cli --host 127.0.0.1 --port 45902 --method Math.Repeat --data "ab"
//   urpc_cli --host 127.0.0.1 --port 45902 --method Nope        --data "x"
//

#include <algorithm>
#include <string>
#include <vector>

#include "uvent/Uvent.h"
#include "uvent/system/SystemContext.h"
#include "ulog/ulog.h"

#include <urpc/server/RPCServer.h>
#include <urpc/router/RPCRouter.h>
#include <urpc/utils/Hash.h>

using namespace usub;
using namespace usub::uvent;

// ---- "user" feature module ------------------------------------------------
//
// Built in isolation. It knows nothing about the server; names are namespaced
// with the "User." prefix, so User.Greet / User.Whoami are registered.
static urpc::RpcRouter make_user_router()
{
    urpc::RpcRouter r{"User."};

    r.route("Greet",
            [](urpc::RpcContext&, std::span<const uint8_t> body)
            -> task::Awaitable<std::string>
            {
                std::string name(
                    reinterpret_cast<const char*>(body.data()), body.size());
                if (name.empty())
                    name = "stranger";
                co_return "Hello, " + name + "!";
            });

    r.route("Whoami",
            [](urpc::RpcContext& ctx, std::span<const uint8_t>)
            -> task::Awaitable<std::string>
            {
                if (ctx.peer && ctx.peer->authenticated)
                    co_return "cn=" + ctx.peer->common_name;
                co_return std::string{"anonymous"};
            });

    return r;
}

// ---- "math" feature module ------------------------------------------------
//
// A stateful router: the handler captures a multiplier. This is exactly what a
// plain function-pointer registry cannot express, and why the router stores
// type-erased handlers.
static urpc::RpcRouter make_math_router(int repeat_factor)
{
    urpc::RpcRouter r{"Math."};

    r.route("Repeat",
            [repeat_factor](urpc::RpcContext&, std::span<const uint8_t> body)
            -> task::Awaitable<std::vector<uint8_t>>
            {
                std::vector<uint8_t> out;
                out.reserve(body.size() * repeat_factor);
                for (int i = 0; i < repeat_factor; ++i)
                    out.insert(out.end(), body.begin(), body.end());
                co_return out;
            });

    return r;
}

int main()
{
    usub::ulog::ULogInit cfg{
        .trace_path = nullptr,
        .debug_path = nullptr,
        .info_path = nullptr,
        .warn_path = nullptr,
        .error_path = nullptr,
        .flush_interval_ns = 2'000'000ULL,
        .queue_capacity = 16384,
        .batch_size = 512,
        .enable_color_stdout = true,
        .max_file_size_bytes = 10 * 1024 * 1024,
        .max_files = 3,
        .json_mode = false,
        .track_metrics = true
    };
    usub::ulog::init(cfg);
    ulog::info("ROUTER: logger initialized");

    urpc::RpcServer server{"0.0.0.0", 45902, 4};
    ulog::info("ROUTER: RpcServer created on port 45902");

    // Compose the standalone routers into one and mount it. The methods are
    // never bound to the server directly — they live in their own routers.
    urpc::RpcRouter api;
    api.merge(make_user_router());
    api.merge(make_math_router(3));

    // Catch-all for any unknown method id.
    api.fallback(
        [](urpc::RpcContext&, std::span<const uint8_t>)
        -> task::Awaitable<std::string>
        {
            co_return std::string{"no such method"};
        });

    server.mount(api);
    ulog::info("ROUTER: mounted api router with {} route(s)", api.size());

    ulog::info("ROUTER: calling server.run()");
    server.run();

    usub::ulog::shutdown();
    return 0;
}
