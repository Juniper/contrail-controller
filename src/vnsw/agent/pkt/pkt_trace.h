/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_trace_hpp
#define vnsw_agent_pkt_trace_hpp

#include <tbb/mutex.h>
#include <boost/circular_buffer.hpp>

class PktTrace {
public:
    static const std::size_t kPktTraceSize = 512;  // number of bytes stored
    static const std::size_t kPktNumBuffers = 16;  // number of buffers stored

    enum Direction {
        In,
        Out
    };

    struct Pkt {
        Direction dir;
        std::size_t len;
        uint8_t pkt[kPktTraceSize];

        Pkt(Direction d, std::size_t l, uint8_t *msg) : dir(d), len(l) {
            memset(pkt, 0, kPktTraceSize);
            memcpy(pkt, msg, std::min(l, kPktTraceSize));
        }
    };

    typedef boost::function<void(PktTrace::Pkt &)> Cb;

    PktTrace() : pkt_trace_(kPktNumBuffers) {};
    virtual ~PktTrace() { pkt_trace_.clear(); };

    void AddPktTrace(Direction dir, std::size_t len, uint8_t *msg) {
        Pkt pkt(dir, len, msg);
        tbb::mutex::scoped_lock lock(mutex_);
        pkt_trace_.push_back(pkt);
    }

    void Clear() { pkt_trace_.clear(); }

    void Iterate(Cb cb) {
        if (cb) {
            tbb::mutex::scoped_lock lock(mutex_);
            for (boost::circular_buffer<Pkt>::iterator it = pkt_trace_.begin();
                 it != pkt_trace_.end(); ++it)
                cb(*it);
        }
    }

private:
    tbb::mutex mutex_;
    boost::circular_buffer<Pkt> pkt_trace_;
};

#endif
