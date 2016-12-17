//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_STRUCTURED_SYSLOG_SERVER_IMPL_H_
#define ANALYTICS_STRUCTURED_SYSLOG_SERVER_IMPL_H_

#include <tbb/mutex.h>

#include <analytics/stat_walker.h>

namespace structured_syslog {
namespace impl {

class StructuredSyslogReader {
 public:
    typedef boost::function<void(
        const std::string &message_name)> ParseFailureCallback;
    StructuredSyslogReader();
    virtual ~StructuredSyslogReader();

 private:
    friend class StructuredSyslogReaderTest;

    tbb::mutex mutex_;
};

bool ProcessStructuredSyslog(const uint8_t *data, size_t len,
    const boost::asio::ip::udp::endpoint &remote_endpoint,
    StatWalker::StatTableInsertFn stat_db_callback, std::vector<std::string> tagged_fields,
    std::vector<std::string> int_fields);

}  // namespace impl
}  // namespace structured_syslog

#endif  // ANALYTICS_STRUCTURED_SYSLOG_SERVER_IMPL_H_
