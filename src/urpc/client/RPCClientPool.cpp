#include <urpc/client/RPCClientPool.h>
#include <ulog/ulog.h>
#include <stdexcept>

namespace urpc
{
    RpcClientPool::RpcClientPool(RpcClientPoolConfig cfg)
        : cfg_(std::move(cfg))
    {
        if (cfg_.max_clients == 0)
            cfg_.max_clients = 1;

        if (cfg_.max_clients == std::numeric_limits<std::size_t>::max())
        {
            clients_.reserve(1024);
        }
        else
        {
            clients_.reserve(cfg_.max_clients);
        }

#if URPC_LOGS
        usub::ulog::info(
            "RpcClientPool: host={} port={} max_clients={} timeout_ms={} ping_interval_ms={}",
            cfg_.host,
            cfg_.port,
            cfg_.max_clients,
            cfg_.socket_timeout_ms,
            cfg_.ping_interval_ms);
#endif
    }

    std::optional<std::size_t> RpcClientPool::try_create_one()
    {
        std::lock_guard<std::mutex> lk(this->create_mx_);
        const std::size_t cur = size_.load(std::memory_order_acquire);
        if (cur >= cfg_.max_clients)
            return std::nullopt;

        RpcClientConfig client_cfg;
        client_cfg.host = cfg_.host;
        client_cfg.port = cfg_.port;
        client_cfg.stream_factory = cfg_.stream_factory;
        client_cfg.socket_timeout_ms = cfg_.socket_timeout_ms;
        client_cfg.ping_interval_ms = cfg_.ping_interval_ms;

        try
        {
            auto client = std::make_shared<RpcClient>(std::move(client_cfg));
            const std::size_t idx =
                clients_.emplace_back(std::move(client));
            size_.store(idx + 1, std::memory_order_release);
#if URPC_LOGS
            usub::ulog::info(
                "RpcClientPool::try_create_one: created client idx={}",
                idx);
#endif
            return idx;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    RpcClientLease RpcClientPool::try_acquire()
    {
        {
            std::size_t cur = size_.load(std::memory_order_acquire);
            if (cur < cfg_.max_clients)
            {
                auto idx_opt = try_create_one();
                if (idx_opt)
                {
                    const std::size_t idx = *idx_opt;
#if URPC_LOGS
                    usub::ulog::debug(
                        "RpcClientPool::try_acquire: new client idx={}",
                        idx);
#endif
                    auto& sp = clients_.at(idx);
                    return RpcClientLease{*sp, idx};
                }
            }
        }

        std::size_t sz = size_.load(std::memory_order_acquire);
        if (sz == 0)
        {
            auto idx_opt = try_create_one();
            if (!idx_opt)
            {
#if URPC_LOGS
                usub::ulog::error(
                    "RpcClientPool::try_acquire: no clients available and "
                    "creation failed");
#endif
                throw std::runtime_error(
                    "RpcClientPool::try_acquire: no clients available and creation failed");
            }
            sz = size_.load(std::memory_order_acquire);
        }

        const std::size_t ticket =
            rr_.fetch_add(1, std::memory_order_acq_rel);

        std::size_t idx;
        if ((sz & (sz - 1)) == 0)
            idx = ticket & (sz - 1);
        else
            idx = ticket % sz;

#if URPC_LOGS
        usub::ulog::debug(
            "RpcClientPool::try_acquire: reuse multiplexed client idx={} "
            "(ticket={}, sz={})",
            idx, ticket, sz);
#endif

        auto& sp = clients_.at(idx);
        return RpcClientLease{*sp, idx};
    }
} // namespace urpc