/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VIZ_COLLECTOR_H_
#define VIZ_COLLECTOR_H_

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/uuid/uuid.hpp>
#include <utility>

#include "base/parse_object.h"
#include "base/contrail_ports.h"

#include "collector.h"
#include "OpServerProxy.h"
#include "viz_constants.h"
#include "syslog_collector.h"
#include "db_handler.h"

class Ruleeng;
class ProtobufCollector;
class SFlowCollector;
class IpfixCollector;

class VizCollector {
public:
    VizCollector(EventManager *evm, unsigned short listen_port,
            bool protobuf_collector_enabled,
            unsigned short protobuf_listen_port,
            const std::vector<std::string> &cassandra_ips,
            const std::vector<int> &cassandra_ports,
            const std::string &redis_uve_ip, unsigned short redis_uve_port,
            const std::string &redis_password,
            const std::string &brokers,
            int syslog_port, int sflow_port, int ipfix_port,
            uint16_t partitions, bool dup,
            const std::string &kafka_prefix, const TtlMap &ttlmap,
            const std::string& cassandra_user,
            const std::string& cassandra_password,
            bool use_cql,
            const std::string &zookeeper_server_list,
            bool use_zookeeper);
    VizCollector(EventManager *evm, DbHandlerPtr db_handler,
                 Ruleeng *ruleeng,
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
        if (rsc) {
            redis_gen_ ++;
            CollectorPublish();
        }
    }
    void SendDbStatistics();
    void SendProtobufCollectorStatistics();
    void SendGeneratorStatistics();
    void TestDatabaseConnection();
    void CollectorPublish();
    bool GetCqlMetrics(cass::cql::Metrics *metrics);

    static const unsigned int kPartCountCnodes = 1;
    static const unsigned int kPartMinTotalCount = 15;
    static std::pair<unsigned int,unsigned int> PartitionRange(
            PartType::type ptype, unsigned int partitions) {
        if (partitions < kPartMinTotalCount) {
            return std::make_pair(0, partitions);
        }
        unsigned int pnodeparts = (partitions / 15) + 1;
        unsigned int varparts = (partitions - 
                kPartCountCnodes - pnodeparts) / 3;
        unsigned int bpart;
        unsigned int npart;
        switch (ptype)
        {
          case PartType::PART_TYPE_CNODES:
            bpart = 0;
            npart = kPartCountCnodes;
            break;

          case PartType::PART_TYPE_PNODES:
            bpart = kPartCountCnodes;
            npart = pnodeparts;
            break;

          case PartType::PART_TYPE_VMS:
            bpart = kPartCountCnodes + pnodeparts;
            npart = varparts;
            break;

          case PartType::PART_TYPE_IFS:
            bpart = kPartCountCnodes + pnodeparts +
                    varparts;
            npart = varparts;
            break;

          default:
            bpart = kPartCountCnodes + pnodeparts +
                    (2*varparts);
            npart = partitions - bpart;
            break;
        }
        return std::make_pair(bpart, npart);
    }
private:
    std::string DbGlobalName(bool dup=false);
    void DbInitializeCb();

    boost::scoped_ptr<DbHandlerInitializer> db_initializer_;
    boost::scoped_ptr<OpServerProxy> osp_;
    boost::scoped_ptr<Ruleeng> ruleeng_;
    Collector *collector_;
    SyslogListeners *syslog_listener_;
    SFlowCollector *sflow_collector_;
    IpfixCollector *ipfix_collector_;
    boost::scoped_ptr<ProtobufCollector> protobuf_collector_;
    std::string name_;
    unsigned short listen_port_;
    uint32_t redis_gen_;
    uint16_t partitions_;
    std::string kafka_prefix_;
    DISALLOW_COPY_AND_ASSIGN(VizCollector);
};

#endif /* VIZ_COLLECTOR_H_ */
