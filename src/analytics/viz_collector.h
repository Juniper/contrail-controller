/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VIZ_COLLECTOR_H_
#define VIZ_COLLECTOR_H_

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "base/parse_object.h"
#include "base/contrail_ports.h"

#include "collector.h"
#include "db_handler.h"
#include "Thrift.h"
#include "OpServerProxy.h"
#include "viz_constants.h"

class Ruleeng;

class VizCollector {
public:
    static const int DbifReinitTime = 10;

    VizCollector(EventManager *evm, unsigned short listen_port,
            std::string cassandra_ip, unsigned short cassandra_port,
            std::string redis_ip, unsigned short redis_port, 
            int gen_timeout = 0, bool dup=false, int analytics_ttl=g_viz_constants.AnalyticsTTL);
    VizCollector(EventManager *evm, DbHandler *db_handler, Ruleeng *ruleeng,
                 Collector *collector, OpServerProxy *osp);
    ~VizCollector();

    std::string name() { return name_; }
    void Init();
    void ReInit();
    void Shutdown();

    DbHandler *GetDbHandler() {
        return db_handler_.get();
    }
    Collector *GetCollector() {
        return collector_;
    }
    Ruleeng *GetRuleeng() {
        return ruleeng_.get();
    }
    OpServerProxy *GetOsp() {
        return osp_.get();
    }
    bool SendRemote(const std::string& destination, const std::string& dec_sandesh);


private:
    EventManager *evm_;
    boost::scoped_ptr<OpServerProxy> osp_;
    boost::scoped_ptr<DbHandler> db_handler_;
    boost::scoped_ptr<Ruleeng> ruleeng_;
    Collector *collector_;
    std::string name_;

    boost::asio::deadline_timer dbif_timer_;

    void Ruleeng_Initialize(); 
    void DbifReinit_fromdb();
    void DbifReinit(const boost::system::error_code &error);
    void StartDbifReinitTimer();
    void StartDbifReinit();

    DISALLOW_COPY_AND_ASSIGN(VizCollector);
};

#endif /* VIZ_COLLECTOR_H_ */
