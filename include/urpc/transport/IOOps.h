#ifndef IOOPS_H
#define IOOPS_H

#include <span>
#include <vector>
#include <urpc/datatypes/Frame.h>
#include <urpc/transport/IRPCStream.h>

namespace urpc {
    using namespace usub::uvent;

    inline task::Awaitable<bool> write_all(
        IRpcStream &stream,
        const uint8_t *data,
        size_t n) {
        size_t off = 0;
        while (off < n) {
            auto *p = const_cast<uint8_t *>(data + off);
            const size_t left = n - off;

            const ssize_t r = co_await stream.async_write(p, left);
            if (r <= 0) co_return false;

            off += static_cast<size_t>(r);
        }
        co_return true;
    }

    inline task::Awaitable<bool> send_frame(
        IRpcStream &stream,
        const RpcFrameHeader &hdr,
        std::span<const uint8_t> payload) {
        std::array<uint8_t, RpcFrameHeaderSize> header_buf{};
        serialize_header(hdr, header_buf.data());
        if (payload.empty())
            co_return co_await write_all(stream, header_buf.data(), header_buf.size());

        std::vector<uint8_t> frame;
        frame.reserve(header_buf.size() + payload.size());
        frame.insert(frame.end(), header_buf.begin(), header_buf.end());
        frame.insert(frame.end(), payload.begin(), payload.end());
        co_return co_await write_all(stream, frame.data(), frame.size());
    }
}

#endif // IOOPS_H
