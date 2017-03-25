/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_log.h"

#include <string>

#include "bgp/routing-instance/routing_instance.h"

SandeshTraceBufferPtr BgpTraceBuf(SandeshTraceBufferCreate(BGP_TRACE_BUF,
        1000));
SandeshTraceBufferPtr BgpPeerObjectTraceBuf(SandeshTraceBufferCreate(
        BGP_PEER_OBJECT_TRACE_BUF, 1000));

namespace bgp_log_test {

bool unit_test_;

static void init_common() {
    unit_test_ = true;

    //
    // By default, we log all messages from all categories.
    //
    Sandesh::SetLoggingParams(true, "", Sandesh::LoggingUtLevel());

    //
    // Have ability to filter messages via environment variables.
    //
    const char *category = getenv("BGP_UT_LOG_CATEGORY");
    if (category) Sandesh::SetLoggingCategory(category);

    const char *level = getenv("BGP_UT_LOG_LEVEL");
    if (level) Sandesh::SetLoggingLevel(level);

    if (getenv("LOG_DISABLE") != NULL) {
        SetLoggingDisabled(true);
    }
}

void init() {
    LoggingInit();
    init_common();
}

void init(std::string log_file, unsigned long log_file_size,
          unsigned long log_file_index, bool enable_syslog,
          std::string syslog_facility, std::string ident,
          std::string log_level) {
    LoggingInit(log_file, log_file_size, log_file_index,
                enable_syslog, syslog_facility, ident,
                SandeshLevelTolog4Level(
                    Sandesh::StringToLevel(log_level)));
    init_common();
}

bool unit_test() {
    return unit_test_;
}

void LogServerName(const BgpServer *server) {
    if (!unit_test_ || !server) return;

    if (Sandesh::LoggingLevel() >= SandeshLevel::SYS_DEBUG) {
        LOG(DEBUG, "BgpServer: " << server->ToString());
    }
}

void LogServerName(const IPeer *ipeer, const BgpTable *table) {
    if (!unit_test_) return;

    BgpServer *server = ipeer ? const_cast<IPeer *>(ipeer)->server() : NULL;
    if (!server && table && table->routing_instance()) {
        server = const_cast<RoutingInstance *>(
                     table->routing_instance())->server();
    }

    LogServerName(server);
}

}  // namespace bgp_log_test
