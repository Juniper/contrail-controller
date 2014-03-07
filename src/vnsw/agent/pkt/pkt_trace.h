/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_trace_hpp
#define vnsw_agent_pkt_trace_hpp

#include <tbb/mutex.h>
#include <boost/scoped_array.hpp>

class PktTrace {
public:
    static const std::size_t kPktMaxTraceSize = 512;  // number of bytes stored
    static const std::size_t kPktNumBuffers = 16;  // number of buffers stored

    enum Direction {
        In,
        Out
    };

    struct Pkt {
        Direction dir;
        std::size_t len;
        uint8_t pkt[kPktMaxTraceSize];

        void Copy(Direction d, std::size_t l, uint8_t *msg, std::size_t pkt_trace_size) {
            dir = d;
            len = l;
            memcpy(pkt, msg, std::min(l, pkt_trace_size));
        }
    };

    typedef boost::function<void(PktTrace::Pkt &)> Cb;

    PktTrace() : pkt_buffer_(new Pkt[kPktNumBuffers]), start_(0), end_(0),
                 count_(0), max_pkt_trace_size_(kPktMaxTraceSize) {}
    virtual ~PktTrace() {}

    void AddPktTrace(Direction dir, std::size_t len, uint8_t *msg) {
        tbb::mutex::scoped_lock lock(mutex_);
        if (!count_) {
            pkt_buffer_[0].Copy(dir, len, msg, max_pkt_trace_size_);
            start_ = end_ = 0;
            count_ = 1;
            return;
        }
        end_ = (end_ + 1) % kPktNumBuffers;
        if (end_ == start_)
            start_ = (start_ + 1) % kPktNumBuffers;
        pkt_buffer_[end_].Copy(dir, len, msg, max_pkt_trace_size_);
        count_ = std::min((count_ + 1), (uint32_t) kPktNumBuffers);
    }

    void Clear() {
        tbb::mutex::scoped_lock lock(mutex_);
        count_ = 0;
    }

    void Iterate(Cb cb) {
        if (cb) {
            tbb::mutex::scoped_lock lock(mutex_);
            for (uint32_t i = 0; i < count_; i++)
                cb(pkt_buffer_[(start_ + i) % kPktNumBuffers]);
        }
    }

    void set_max_pkt_trace_size(std::size_t size) {
        max_pkt_trace_size_ = std::min(size, kPktMaxTraceSize);
    }

private:
    tbb::mutex mutex_;
    boost::scoped_array<Pkt> pkt_buffer_;
    uint32_t start_;
    uint32_t end_;
    uint32_t count_;
    std::size_t max_pkt_trace_size_;
};

#endif
