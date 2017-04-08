//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_STRUCTURED_SYSLOG_SERVER_IMPL_H_
#define ANALYTICS_STRUCTURED_SYSLOG_SERVER_IMPL_H_

#include <analytics/stat_walker.h>

namespace structured_syslog {
namespace impl {

bool ProcessStructuredSyslog(const uint8_t *data, size_t len,
    const boost::asio::ip::address remote_address,
    StatWalker::StatTableInsertFn stat_db_callback, StructuredSyslogConfig *config_obj,
    boost::shared_ptr<StructuredSyslogForwarder> forwarder);

}  // namespace impl
}  // namespace structured_syslog

#endif  // ANALYTICS_STRUCTURED_SYSLOG_SERVER_IMPL_H_

