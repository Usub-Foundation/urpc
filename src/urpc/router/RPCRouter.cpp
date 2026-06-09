#include <urpc/router/RPCRouter.h>
#include <urpc/registry/RPCMethodRegistry.h>

namespace urpc
{
    RpcRouter::RpcRouter(std::string prefix)
        : prefix_(std::move(prefix))
    {
    }

    uint64_t RpcRouter::key_for(std::string_view name) const
    {
        if (this->prefix_.empty())
            return fnv1a64_rt(name);

        std::string full;
        full.reserve(this->prefix_.size() + name.size());
        full.append(this->prefix_);
        full.append(name);
        return fnv1a64_rt(full);
    }

    RpcRouter& RpcRouter::on(uint64_t method_id, RpcHandler handler)
    {
        this->handlers_[method_id] = std::move(handler);
        return *this;
    }

    RpcRouter& RpcRouter::on(std::string_view name, RpcHandler handler)
    {
        this->handlers_[this->key_for(name)] = std::move(handler);
        return *this;
    }

    RpcRouter& RpcRouter::merge(const RpcRouter& other)
    {
        for (const auto& [id, handler] : other.handlers_)
            this->handlers_[id] = handler;

        if (!this->not_found_ && other.not_found_)
            this->not_found_ = other.not_found_;

        return *this;
    }

    void RpcRouter::set_fallback_registry(
        const RpcMethodRegistry* registry) noexcept
    {
        this->fallback_registry_ = registry;
    }

    const RpcHandler* RpcRouter::find(uint64_t method_id) const
    {
        const auto it = this->handlers_.find(method_id);
        return it == this->handlers_.end() ? nullptr : &it->second;
    }

    RpcRoute RpcRouter::resolve(uint64_t method_id) const
    {
        if (const RpcHandler* h = this->find(method_id))
            return RpcRoute{.fn = h, .raw = nullptr};

        if (this->fallback_registry_)
        {
            if (RpcHandlerPtr raw = this->fallback_registry_->find(method_id))
                return RpcRoute{.fn = nullptr, .raw = raw};
        }

        if (this->not_found_)
            return RpcRoute{.fn = &this->not_found_, .raw = nullptr};

        return RpcRoute{};
    }
}
