//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_STRUCTURED_SYSLOG_SERVER_H_
#define ANALYTICS_STRUCTURED_SYSLOG_SERVER_H_

#include <vector>

#include <boost/system/error_code.hpp>
#include <boost/asio/ip/udp.hpp>

#include <analytics/stat_walker.h>
#include <analytics/structured_syslog_config.h>

namespace structured_syslog {

//
// StructuredSyslog Server
//
class StructuredSyslogServer {
 public:
    StructuredSyslogServer(EventManager *evm, uint16_t port,
        const vector<string> structured_syslog_forward_dst,
        boost::shared_ptr<ConfigDBConnection> cfgdb_connection,
        StatWalker::StatTableInsertFn stat_db_cb);
    virtual ~StructuredSyslogServer();
    bool Initialize();
    void Shutdown();
    boost::asio::ip::udp::endpoint GetLocalEndpoint(
        boost::system::error_code *ec);

 private:
    class StructuredSyslogServerImpl;
    StructuredSyslogServerImpl *impl_;
};

class  StructuredSyslogQueueEntry
{
public:
    size_t      length;
    std::string *data;

    StructuredSyslogQueueEntry (std::string *d, size_t len);
    virtual ~StructuredSyslogQueueEntry ();
};

class StructuredSyslogTcpForwarder;

class StructuredSyslogForwarder
{
public:
    StructuredSyslogForwarder (EventManager *evm, const vector <std::string> forward_dst);
    virtual ~StructuredSyslogForwarder ();
    void Forward (StructuredSyslogQueueEntry *sqe);
    void Shutdown ();

protected:
    bool PollTcpForwarder();
    void PollTcpForwarderErrorHandler (string error_name, string error_message);
    void Init (const vector <std::string> forward_dst);
    bool Client (StructuredSyslogQueueEntry *sqe);
private:
    EventManager                           *evm_;
    vector <std::string>                   forward_dst_;
    WorkQueue<StructuredSyslogQueueEntry*> work_queue_;
    std::vector<StructuredSyslogTcpForwarder*> tcpForwarder_;
    Timer                                  *tcpForwarder_poll_timer_;
    static const int tcpForwarderPollInterval = 60 * 1000; // in ms
};

}  // namespace structured_syslog

#endif  // ANALYTICS_STRUCTURED_SYSLOG_SERVER_H_

