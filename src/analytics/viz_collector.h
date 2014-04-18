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
class Ruleeng;

class VizCollector {
public:
    static const int DbifReinitTime = 10;

    VizCollector(EventManager *evm, unsigned short listen_port,
            std::string cassandra_ip, unsigned short cassandra_port,
            const std::string redis_uve_ip, unsigned short redis_uve_port,
            int syslog_port, bool dup=false,
            int analytics_ttl=g_viz_constants.AnalyticsTTL);
    VizCollector(EventManager *evm, DbHandler *db_handler, Ruleeng *ruleeng,
                 Collector *collector, OpServerProxy *osp);
    ~VizCollector();

    std::string name() { return name_; }
    bool Init();
    void Shutdown();
    static void WaitForIdle();

    DbHandler *GetDbHandler() const {
        return db_handler_.get();
    }
    SyslogListeners *GetSyslogListener () const {
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
    EventManager *evm_;
    boost::scoped_ptr<OpServerProxy> osp_;
    boost::scoped_ptr<DbHandler> db_handler_;
    boost::scoped_ptr<Ruleeng> ruleeng_;
    Collector *collector_;
    SyslogListeners *syslog_listener_;
    std::string name_;

    Timer *dbif_timer_;

    void Ruleeng_Initialize(); 
    void DbifReinit_fromdb();
    bool DbifReinitTimerExpired();
    void DbifReinitTimerErrorHandler(std::string error_name, std::string error_message);
    void StartDbifReinitTimer();
    void StartDbifReinit();

    std::string DbifGlobalName(bool dup=false);

    DISALLOW_COPY_AND_ASSIGN(VizCollector);
};

#endif /* VIZ_COLLECTOR_H_ */
