//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <sandesh/sandesh_trace.h>

#include "base/util.h"
#include "io/io_types.h"
#include "io/io_utils.h"

namespace io {

SocketStats::SocketStats() {
    read_calls = 0;
    read_bytes = 0;
    read_errors = 0;
    write_calls = 0;
    write_bytes = 0;
    write_errors = 0;
    write_blocked = 0;
    write_blocked_duration_usecs = 0;
}

void SocketStats::GetRxStats(SocketIOStats &socket_stats) const {
    socket_stats.calls = read_calls;
    socket_stats.bytes = read_bytes;
    if (read_calls) {
        socket_stats.average_bytes = read_bytes/read_calls;
    }
}

void SocketStats::GetTxStats(SocketIOStats &socket_stats) const {
    socket_stats.calls = write_calls;
    socket_stats.bytes = write_bytes;
    if (write_calls) {
        socket_stats.average_bytes = write_bytes/write_calls;
    }
    socket_stats.blocked_count = write_blocked;
    socket_stats.blocked_duration = duration_usecs_to_string(
        write_blocked_duration_usecs);
    if (write_blocked) {
        socket_stats.average_blocked_duration =
                 duration_usecs_to_string(
                     write_blocked_duration_usecs/
                     write_blocked);
    }
}

}  // namespace io
