/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "viz_collector.h"
#include <boost/asio/ip/host_name.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <vector>

#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include "io/event_manager.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_session.h"

#include "ruleeng.h"
#include "protobuf_collector.h"
#include "sflow_collector.h"
#include "ipfix_collector.h"
#include "viz_sandesh.h"

using std::stringstream;
using std::string;
using std::map;
using std::make_pair;
using boost::system::error_code;

VizCollector::VizCollector(EventManager *evm, unsigned short listen_port,
            bool protobuf_collector_enabled,
            unsigned short protobuf_listen_port,
            const std::vector<std::string> &cassandra_ips,
            const std::vector<int> &cassandra_ports,
            const std::string &redis_uve_ip, unsigned short redis_uve_port,
            const std::string &redis_password,
            const std::string &brokers,
            int syslog_port, int sflow_port, int ipfix_port,
            uint16_t partitions, bool dup,
            const std::string &kafka_prefix, const TtlMap& ttl_map,
            const std::string &cassandra_user,
            const std::string &cassandra_password,
            const std::string &cassandra_compaction_strategy,
            const std::string &cassandra_flow_tables_compaction_strategy,
            const std::string &zookeeper_server_list,
            bool use_zookeeper, bool disable_all_db_writes,
            bool disable_db_stats_writes, bool disable_db_messages_writes,
            bool disable_db_messages_keyword_writes) :
    db_initializer_(new DbHandlerInitializer(evm, DbGlobalName(dup), -1,
        std::string("collector:DbIf"),
        boost::bind(&VizCollector::DbInitializeCb, this),
        cassandra_ips, cassandra_ports, ttl_map, cassandra_user,
        cassandra_password, cassandra_compaction_strategy,
        cassandra_flow_tables_compaction_strategy,
        zookeeper_server_list, use_zookeeper,
        disable_all_db_writes, disable_db_stats_writes,
        disable_db_messages_writes, disable_db_messages_keyword_writes)),
    osp_(new OpServerProxy(evm, this, redis_uve_ip, redis_uve_port,
         redis_password, brokers, partitions, kafka_prefix)),
    ruleeng_(new Ruleeng(db_initializer_->GetDbHandler(), osp_.get())),
    collector_(new Collector(evm, listen_port, db_initializer_->GetDbHandler(),
        osp_.get(),
        boost::bind(&Ruleeng::rule_execute, ruleeng_.get(), _1, _2, _3, _4),
        cassandra_ips, cassandra_ports, ttl_map, cassandra_user,
        cassandra_password)),
    syslog_listener_(new SyslogListeners(evm,
            boost::bind(&Ruleeng::rule_execute, ruleeng_.get(), _1, _2, _3, _4),
            db_initializer_->GetDbHandler(), syslog_port)),
    sflow_collector_(new SFlowCollector(evm, db_initializer_->GetDbHandler(),
        std::string(), sflow_port)),
    ipfix_collector_(new IpfixCollector(evm, db_initializer_->GetDbHandler(),
        string(), ipfix_port)),
    redis_gen_(0), partitions_(partitions) {
    error_code error;
    if (dup)
        name_ = boost::asio::ip::host_name(error) + "dup";
    else
        name_ = boost::asio::ip::host_name(error);
    if (protobuf_collector_enabled) {
        protobuf_collector_.reset(new ProtobufCollector(evm,
            protobuf_listen_port, cassandra_ips, cassandra_ports,
            ttl_map, cassandra_user, cassandra_password,
            db_initializer_->GetDbHandler()));
    }
}

void
VizCollector::CollectorPublish(bool rsc)
{
    if (!collector_) return;
    DiscoveryServiceClient *ds_client = Collector::GetCollectorDiscoveryServiceClient();
    if (!ds_client) return;
    string service_name = g_vns_constants.COLLECTOR_DISCOVERY_SERVICE_NAME;
    if (!rsc) {
        ds_client->WithdrawPublish(service_name);
        return;
    }
    stringstream pub_ss;
    pub_ss << "<" << service_name << "><ip-address>" << Collector::GetSelfIp() <<
            "</ip-address><port>" << collector_->GetPort() <<
            "</port><pid>" << getpid() << 
            "</pid><redis-gen>" << redis_gen_ <<
            "</redis-gen><partcount>{ \"1\":[" <<
                           VizCollector::PartitionRange(
                    PartType::PART_TYPE_CNODES,partitions_).first <<
            "," << VizCollector::PartitionRange(
                    PartType::PART_TYPE_CNODES,partitions_).second << 
            "], \"2\":[" << VizCollector::PartitionRange(
                    PartType::PART_TYPE_PNODES,partitions_).first <<
            "," << VizCollector::PartitionRange(
                    PartType::PART_TYPE_PNODES,partitions_).second << 
            "], \"3\":[" << VizCollector::PartitionRange(
                    PartType::PART_TYPE_VMS,partitions_).first <<
            "," << VizCollector::PartitionRange(
                    PartType::PART_TYPE_VMS,partitions_).second << 
            "], \"4\":[" << VizCollector::PartitionRange(
                    PartType::PART_TYPE_IFS,partitions_).first <<
            "," << VizCollector::PartitionRange(
                    PartType::PART_TYPE_IFS,partitions_).second << 
            "], \"5\":[" << VizCollector::PartitionRange(
                    PartType::PART_TYPE_OTHER,partitions_).first <<
            "," << VizCollector::PartitionRange(
                    PartType::PART_TYPE_OTHER,partitions_).second << 
            "]}</partcount></"  << service_name << ">";
    std::string pub_msg;
    pub_msg = pub_ss.str();
    ds_client->Publish(service_name, pub_msg);
}

VizCollector::VizCollector(EventManager *evm, DbHandlerPtr db_handler,
        Ruleeng *ruleeng, Collector *collector, OpServerProxy *osp) :
    db_initializer_(new DbHandlerInitializer(evm, DbGlobalName(false), -1,
        std::string("collector::DbIf"),
        boost::bind(&VizCollector::DbInitializeCb, this),
        db_handler)),
    osp_(osp),
    ruleeng_(ruleeng),
    collector_(collector),
    syslog_listener_(new SyslogListeners (evm,
            boost::bind(&Ruleeng::rule_execute, ruleeng, _1, _2, _3, _4),
            db_handler)),
    sflow_collector_(NULL), ipfix_collector_(NULL), redis_gen_(0), partitions_(0) {
    error_code error;
    name_ = boost::asio::ip::host_name(error);
}

VizCollector::~VizCollector() {
}

std::string VizCollector::DbGlobalName(bool dup) {
    return collector_->DbGlobalName(dup);
}

bool VizCollector::SendRemote(const string& destination,
        const string& dec_sandesh) {
    if (collector_){
        return collector_->SendRemote(destination, dec_sandesh);
    } else {
        return false;
    }
}

void VizCollector::WaitForIdle() {
    static const int kTimeout = 15;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    for (int i = 0; i < (kTimeout * 1000); i++) {
        if (scheduler->IsEmpty()) {
            break;
        }
        usleep(1000);
    }
}

void VizCollector::Shutdown() {
    // First shutdown collector
    collector_->Shutdown();
    WaitForIdle();

    // Wait until all connections are cleaned up.
    for (int cnt = 0; collector_->ConnectionsCount() != 0 && cnt < 15; cnt++) {
        sleep(1);
    }
    TcpServerManager::DeleteServer(collector_);

    osp_->Shutdown();
    syslog_listener_->Shutdown();
    WaitForIdle();

    if (protobuf_collector_) {
        protobuf_collector_->Shutdown();
        WaitForIdle();
    }
    if (sflow_collector_) {
        sflow_collector_->Shutdown();
        WaitForIdle();
        UdpServerManager::DeleteServer(sflow_collector_);
    }
    if (ipfix_collector_) {
        ipfix_collector_->Shutdown();
        WaitForIdle();
        UdpServerManager::DeleteServer(ipfix_collector_);
    }

    db_initializer_->Shutdown();
    LOG(DEBUG, __func__ << " viz_collector done");
}

void VizCollector::DbInitializeCb() {
    ruleeng_->Init();
    if (!syslog_listener_->IsRunning()) {
        syslog_listener_->Start();
        LOG(DEBUG, __func__ << " Initialization of syslog listener done!");
    }
    if (protobuf_collector_) {
        protobuf_collector_->Initialize();
    }
    if (sflow_collector_) {
        sflow_collector_->Start();
    }
    if (ipfix_collector_) {
        ipfix_collector_->Start();
    }
}

bool VizCollector::Init() {
    return db_initializer_->Initialize();
}

void VizCollector::SendProtobufCollectorStatistics() {
    if (protobuf_collector_) {
        protobuf_collector_->SendStatistics(name_);
    }
}

void VizCollector::SendGeneratorStatistics() {
    if (collector_) {
        collector_->SendGeneratorStatistics();
    }
}

void VizCollector::SendDbStatistics() {
    DbHandlerPtr db_handler(db_initializer_->GetDbHandler());
    // DB stats
    std::vector<GenDb::DbTableInfo> vdbti, vstats_dbti;
    GenDb::DbErrors dbe;
    db_handler->GetStats(&vdbti, &dbe, &vstats_dbti);

    // TODO: Change DBStats to return a map directly
    map<string,GenDb::DbTableStat> mtstat, msstat;

    for (size_t idx=0; idx<vdbti.size(); idx++) {
        GenDb::DbTableStat dtis;
        dtis.set_reads(vdbti[idx].get_reads());
        dtis.set_read_fails(vdbti[idx].get_read_fails());
        dtis.set_writes(vdbti[idx].get_writes());
        dtis.set_write_fails(vdbti[idx].get_write_fails());
        dtis.set_write_back_pressure_fails(vdbti[idx].get_write_back_pressure_fails());
        mtstat.insert(make_pair(vdbti[idx].get_table_name(), dtis));
    } 

    for (size_t idx=0; idx<vstats_dbti.size(); idx++) {
        GenDb::DbTableStat dtis;
        dtis.set_reads(vstats_dbti[idx].get_reads());
        dtis.set_read_fails(vstats_dbti[idx].get_read_fails());
        dtis.set_writes(vstats_dbti[idx].get_writes());
        dtis.set_write_fails(vstats_dbti[idx].get_write_fails());
        dtis.set_write_back_pressure_fails(
            vstats_dbti[idx].get_write_back_pressure_fails());
        msstat.insert(make_pair(vstats_dbti[idx].get_table_name(), dtis));
    } 
    
    CollectorDbStats cds;
    cds.set_name(name_);
    cds.set_table_info(mtstat);
    cds.set_errors(dbe);
    cds.set_stats_info(msstat);

    cass::cql::DbStats cql_stats;
    if (db_handler->GetCqlStats(&cql_stats)) {
        cds.set_cql_stats(cql_stats);
    }
    CollectorDbStatsTrace::Send(cds);
}

bool VizCollector::GetCqlMetrics(cass::cql::Metrics *metrics) {
    DbHandlerPtr db_handler(db_initializer_->GetDbHandler());
    return db_handler->GetCqlMetrics(metrics);
}
