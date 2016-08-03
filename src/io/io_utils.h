//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef SRC_IO_IO_UTILS_H_
#define SRC_IO_IO_UTILS_H_

#include <tbb/atomic.h>

class SocketIOStats;

namespace io {

struct SocketStats {
    SocketStats();

    void GetRxStats(SocketIOStats *socket_stats) const;
    void GetTxStats(SocketIOStats *socket_stats) const;

    tbb::atomic<uint64_t> read_calls;
    tbb::atomic<uint64_t> read_bytes;
    tbb::atomic<uint64_t> read_errors;
    tbb::atomic<uint64_t> write_calls;
    tbb::atomic<uint64_t> write_bytes;
    tbb::atomic<uint64_t> write_errors;
    tbb::atomic<uint64_t> write_blocked;
    tbb::atomic<uint64_t> write_blocked_duration_usecs;
    tbb::atomic<uint64_t> read_block_start_time;
    tbb::atomic<uint64_t> read_blocked;
    tbb::atomic<uint64_t> read_blocked_duration_usecs;
};

}  // namespace io

#endif  // SRC_IO_IO_UTILS_H_
