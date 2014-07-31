/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VIZ_COLLECTOR_H_
#define VIZ_COLLECTOR_H_

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/uuid/uuid.hpp>

#include "base/parse_object.h"
#include "base/contrail_ports.h"

#include "collector.h"
#include "Thrift.h"
#include "OpServerProxy.h"
#include "viz_constants.h"
#include "syslog_collector.h"

class DbHandler;
class DbHandlerInitializer;
class Ruleeng;
class ProtobufCollector;

class VizCollector {
public:
    VizCollector(EventManager *evm, unsigned short listen_port,
            bool protobuf_collector_enabled,
            unsigned short protobuf_listen_port,
            const std::vector<std::string> &cassandra_ips,
            const std::vector<int> &cassandra_ports,
            const std::string &redis_uve_ip, unsigned short redis_uve_port,
            int syslog_port, bool dup=false,
            int analytics_ttl=g_viz_constants.AnalyticsTTL);
    VizCollector(EventManager *evm, DbHandler *db_handler, Ruleeng *ruleeng,
                 Collector *collector, OpServerProxy *osp);
    ~VizCollector();

    std::string name() { return name_; }
    bool Init();
    void Shutdown();
    static void WaitForIdle();

    SyslogListeners *GetSyslogListener() const {
        return syslog_listener_;
    }
    Collector *GetCollector() const {
        return collector_;
    }
    Ruleeng *GetRuleeng() const {
        return ruleeng_.get();
    }
    OpServerProxy *GetOsp() const {
        return osp_.get();
    }
    bool SendRemote(const std::string& destination, const std::string& dec_sandesh);
    void RedisUpdate(bool rsc) {
        collector_->RedisUpdate(rsc);
    }

private:
    std::string DbGlobalName(bool dup=false);
    void DbInitializeCb();

    boost::scoped_ptr<DbHandlerInitializer> db_initializer_;
    boost::scoped_ptr<OpServerProxy> osp_;
    boost::scoped_ptr<Ruleeng> ruleeng_;
    Collector *collector_;
    SyslogListeners *syslog_listener_;
    boost::scoped_ptr<ProtobufCollector> protobuf_collector_;
    std::string name_;

    DISALLOW_COPY_AND_ASSIGN(VizCollector);
};

#endif /* VIZ_COLLECTOR_H_ */
