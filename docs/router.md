# Request Router

`RpcRouter` is a standalone, composable collection of method handlers. Instead
of binding every handler directly onto an `RpcServer`, you build one or more
routers — wherever you like, in whatever translation unit you like — and then
`mount()` them onto the server. This decouples *handler definition* from the
*server instance*.

It is the moral equivalent of a routing table / sub-router in an HTTP
framework: feature modules expose a router, and the application wires them
together.

---

## Why a router

The low-level `RpcMethodRegistry` stores plain function pointers
(`RpcHandlerPtr`). That is fast, but it cannot hold a handler that captures
state — every handler must be a captureless lambda.

`RpcRouter` stores **type-erased handlers** (`RpcHandler =
std::function<...>`), which means a route can capture state: a database
handle, a service object, a configuration value, a middleware wrapper. This is
exactly what you need to build handlers in isolation and compose them later.

| | `RpcMethodRegistry` | `RpcRouter` |
|---|---|---|
| Handler type | function pointer | `std::function` (stateful) |
| Namespacing | manual | built-in prefix |
| Composition | — | `merge()` / `mount()` |
| Unknown method | 404 | 404 **or** `fallback()` |

The two interoperate: the server's router resolves a request against its own
routes first, then falls back to the legacy registry, then to a not-found
handler. Existing `register_method_ct` / `register_method` code keeps working
unchanged.

---

## Building a router

```cpp
#include <urpc/router/RPCRouter.h>

// A namespaced router. The prefix is prepended to every *name* before it is
// hashed, so on/route("Get", …) registers the method id of "User.Get".
urpc::RpcRouter make_user_router()
{
    urpc::RpcRouter r{"User."};

    // route(): name-based, result is auto-wrapped (string/bytes/void).
    r.route("Greet",
        [](urpc::RpcContext&, std::span<const uint8_t> body)
            -> Awaitable<std::string>
        {
            std::string name((const char*)body.data(), body.size());
            co_return "Hello, " + (name.empty() ? "stranger" : name) + "!";
        });

    return r;
}
```

Routers can capture state — the value `register_method_ct` cannot express:

```cpp
urpc::RpcRouter make_math_router(int factor)
{
    urpc::RpcRouter r{"Math."};

    r.route("Repeat",
        [factor](urpc::RpcContext&, std::span<const uint8_t> body)
            -> Awaitable<std::vector<uint8_t>>
        {
            std::vector<uint8_t> out;
            for (int i = 0; i < factor; ++i)
                out.insert(out.end(), body.begin(), body.end());
            co_return out;
        });

    return r;
}
```

---

## Registration API

| Method | Id source | Auto-wraps result | Prefix applied |
|---|---|---|---|
| `on(uint64_t, RpcHandler)` | explicit id | no | no |
| `on(string_view, RpcHandler)` | hash(name) | no | yes |
| `on_ct<MethodId>(F&&)` | compile-time id | yes | no |
| `route(string_view, F&&)` | hash(name) | yes | yes |
| `fallback(F&&)` | — (catch-all) | yes | — |

"Auto-wraps result" means the handler may return
`Awaitable<std::vector<uint8_t>>`, `Awaitable<R>` for any `ByteRange` `R`
(e.g. `std::string`), or `Awaitable<void>` — identical to
`RpcServer::register_method_ct`.

---

## Composing and mounting

```cpp
urpc::RpcServer server{"0.0.0.0", 45902, 4};

urpc::RpcRouter api;
api.merge(make_user_router());     // copy in User.* routes
api.merge(make_math_router(3));    // copy in Math.* routes

// Optional catch-all for any unknown method id.
api.fallback([](urpc::RpcContext&, std::span<const uint8_t>)
                 -> Awaitable<std::string>
             { co_return std::string{"no such method"}; });

server.mount(api);                 // attach to the server
server.run();
```

* `merge(other)` copies every route from `other`; on a method-id collision the
  entry from `other` wins. If the destination has no `fallback` yet, it adopts
  `other`'s.
* `mount(router)` on the server merges the router into the server's own
  dispatch router. Mount as many as you like.

`server.router()` exposes the server's dispatch router directly if you want to
register onto it without an intermediate object.

---

## Resolution order

For each incoming request the connection calls `router.resolve(method_id)`,
which checks, in order:

1. the router's own routes,
2. the legacy `RpcMethodRegistry` (so `server.registry()` and
   `register_method*` registrations still resolve),
3. the `fallback()` handler, if one is set.

Only when all three miss does the server send the built-in `404 Unknown
method` error. With a `fallback()` installed, unknown methods reach your
catch-all instead of producing a 404.

---

## Notes

* Build routers **before** `server.run()`. Like the registry, the route map is
  read concurrently by per-connection coroutines once the server is running and
  is not synchronised for live mutation.
* A mounted router's handlers are **copied** into the server's router. The
  source `RpcRouter` object does not need to outlive the server.
* Captured state inside a handler **does** need to outlive the server (it is
  owned by the handler via a `shared_ptr`, so capturing by value is the safe
  default).

See `examples/main_router.cpp` for a complete, runnable example.
