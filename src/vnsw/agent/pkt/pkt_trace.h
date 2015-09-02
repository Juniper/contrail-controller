/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_trace_hpp
#define vnsw_agent_pkt_trace_hpp

#include <boost/scoped_array.hpp>

class AgentHdr;

class PktTrace {
public:
    static const std::size_t kPktMaxTraceSize = 512;  // number of bytes stored
    static const std::size_t kPktNumBuffers = 16;     // number of buffers stored
    static const std::size_t kPktMaxNumBuffers = 512000;

    enum Direction {
        In,
        Out,
        Invalid
    };

    struct Pkt {
        Direction dir;
        std::size_t len;
        uint8_t pkt[kPktMaxTraceSize];

        Pkt() : dir(Invalid), len(0) {}
        void Copy(Direction d, std::size_t l, uint8_t *msg,
                  std::size_t pkt_trace_size, const AgentHdr *hdr);
    };

    typedef boost::function<void(PktTrace::Pkt &)> Cb;

    PktTrace() : end_(-1), count_(0), num_buffers_(kPktNumBuffers),
                 pkt_trace_size_(kPktMaxTraceSize) {
                 pkt_buffer_.resize(num_buffers_);
    }
    virtual ~PktTrace() {}

    void AddPktTrace(Direction dir, std::size_t len, uint8_t *msg,
                     const AgentHdr *hdr);
    void Clear() {
        count_ = 0;
        end_ = -1;
    }

    void Iterate(Cb cb) {
        if (cb && count_) {
            uint32_t start_ =
                (count_ < num_buffers_) ? 0 : (end_ + 1) % num_buffers_;
            for (uint32_t i = 0; i < count_; i++)
                cb(pkt_buffer_[(start_ + i) % num_buffers_]);
        }
    }

    std::size_t num_buffers() const { return num_buffers_; }
    std::size_t pkt_trace_size() const { return pkt_trace_size_; }

    void set_pkt_trace_size(std::size_t size) {
        pkt_trace_size_ = std::min(size, kPktMaxTraceSize);
    }

    // change number of buffers
    void set_num_buffers(uint32_t num_buffers) {
        if (num_buffers_ != num_buffers) {
            // existing buffers are cleared upon resizing
            count_ = 0;
            end_ = -1;
            num_buffers_ = num_buffers;
            pkt_buffer_.resize(num_buffers_);
        }
    }

private:
    uint32_t end_;
    uint32_t count_;
    std::size_t num_buffers_;
    std::size_t pkt_trace_size_;
    std::vector<Pkt> pkt_buffer_;
};

#endif
