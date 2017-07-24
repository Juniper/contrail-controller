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
#include <analytics/structured_syslog_kafka_forwarder.h>

namespace structured_syslog {

//
// StructuredSyslog Server
//
class StructuredSyslogServer {
 public:
    StructuredSyslogServer(EventManager *evm, uint16_t port,
        const vector<string> &structured_syslog_tcp_forward_dst,
        const std::string &structured_syslog_kafka_broker,
        const std::string &structured_syslog_kafka_topic,
        uint16_t structured_syslog_kafka_partitions,
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

class  StructuredSyslogQueueEntry {
public:
    size_t      length;
    boost::shared_ptr<std::string> data;
    boost::shared_ptr<std::string> json_data;
    boost::shared_ptr<std::string> skey;

    StructuredSyslogQueueEntry(boost::shared_ptr<std::string> d, size_t len,
                               boost::shared_ptr<std::string> jd,
                               boost::shared_ptr<std::string> key);
    virtual ~StructuredSyslogQueueEntry();
};

class StructuredSyslogTcpForwarderSession;

class StructuredSyslogTcpForwarder : public TcpServer {
public:
    StructuredSyslogTcpForwarder(EventManager *evm, const std::string &ipaddress, int port);

    virtual TcpSession *AllocSession(Socket *socket);
    void WriteReady(const boost::system::error_code &ec);
    void Connect();
    bool Send(const u_int8_t *data, size_t size, size_t *actual);
    bool Connected();
    void Shutdown();
    StructuredSyslogTcpForwarderSession *GetSession() const { return session_; }
    void SetSocketOptions();
    std::string GetIpAddress() {return ipaddress_;}
    int GetPort(){return port_;}

private:
    std::string ipaddress_;
    int port_;
    StructuredSyslogTcpForwarderSession *session_;
    tbb::mutex send_mutex_;
    bool ready_to_send_;

};

class StructuredSyslogForwarder {
public:
    StructuredSyslogForwarder(EventManager *evm, const vector <std::string> &tcp_forward_dst,
                               const std::string &structured_syslog_kafka_broker,
                               const std::string &structured_syslog_kafka_topic,
                               uint16_t structured_syslog_kafka_partitions);

    virtual ~StructuredSyslogForwarder();
    void Forward(boost::shared_ptr<StructuredSyslogQueueEntry> sqe);
    void Shutdown();
    bool kafkaForwarder();

protected:
    bool PollTcpForwarder();
    void PollTcpForwarderErrorHandler(string error_name, string error_message);
    void Init(const vector <std::string> &tcp_forward_dst,
               const std::string &structured_syslog_kafka_broker,
               const std::string &structured_syslog_kafka_topic,
               uint16_t structured_syslog_kafka_partitions);
private:
    EventManager                           *evm_;
    std::vector<boost::shared_ptr<StructuredSyslogTcpForwarder> > tcpForwarder_;
    KafkaForwarder*                            kafkaForwarder_;
    Timer                                  *tcpForwarder_poll_timer_;
    static const int tcpForwarderPollInterval = 60 * 1000; // in ms
};

}  // namespace structured_syslog

#endif  // ANALYTICS_STRUCTURED_SYSLOG_SERVER_H_

