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

#include "sandesh/sandesh_session.h"

#include "ruleeng.h"
#include "protobuf_collector.h"
#include "structured_syslog_collector.h"
#include "sflow_collector.h"
#include "ipfix_collector.h"
#include "viz_sandesh.h"
#include <zookeeper/zookeeper_client.h>

using std::stringstream;
using std::string;
using std::map;
using std::make_pair;
using boost::system::error_code;
using namespace zookeeper::client;

VizCollector::VizCollector(EventManager *evm, unsigned short listen_port,
            bool protobuf_collector_enabled,
            unsigned short protobuf_listen_port,
            const std::string protobuf_schema_file_directory,
            bool structured_syslog_collector_enabled,
            unsigned short structured_syslog_listen_port,
            const vector<string> &structured_syslog_tcp_forward_dst,
            const std::string &structured_syslog_kafka_broker,
            const std::string &structured_syslog_kafka_topic,
            uint16_t structured_syslog_kafka_partitions,
            const std::string &redis_uve_ip, unsigned short redis_uve_port,
            const std::string &redis_password,
            const std::map<std::string, std::string>& aggconf,
            const std::string &brokers,
            int syslog_port, int sflow_port, int ipfix_port,
            uint16_t partitions, bool dup,
            const std::string &kafka_prefix,
            const Options::Cassandra &cassandra_options,
            const std::string &zookeeper_server_list,
            bool use_zookeeper,
            const DbWriteOptions &db_write_options,
            const SandeshConfig &sandesh_config,
            ConfigClientCollector *config_client,
            bool grok_enabled,
            const std::vector<std::string> &grok_key_list,
            const std::vector<std::string> &grok_attrib_list,
            std::string host_ip) :
    db_initializer_(new DbHandlerInitializer(evm, DbGlobalName(dup),
        std::string("collector:DbIf"),
        boost::bind(&VizCollector::DbInitializeCb, this),
        cassandra_options,
        zookeeper_server_list, use_zookeeper,
        db_write_options, config_client)),
    osp_(new OpServerProxy(evm, this, redis_uve_ip, redis_uve_port,
         redis_password, aggconf, brokers, partitions, kafka_prefix)),
    ruleeng_(new Ruleeng(db_initializer_->GetDbHandler(), osp_.get())),
    collector_(new Collector(evm, listen_port, sandesh_config,
                             db_initializer_->GetDbHandler(),
        osp_.get(),
        boost::bind(&Ruleeng::rule_execute, ruleeng_.get(), _1, _2, _3, _4))),
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
            protobuf_listen_port, protobuf_schema_file_directory,
            db_initializer_->GetDbHandler()));
    }
    if (grok_enabled) {
        gp_.reset(new GrokParser());
        gp_.get()->init();
        gp_.get()->msg_type_add("APPTRACK_SESSION_CLOSE");
        gp_.get()->set_key_list(grok_key_list);
        gp_.get()->set_attrib_list(grok_attrib_list);
    }
    else {
        gp_.reset();
    }
    if (structured_syslog_collector_enabled) {
        structured_syslog_collector_.reset(new StructuredSyslogCollector(evm,
            structured_syslog_listen_port, structured_syslog_tcp_forward_dst,
            structured_syslog_kafka_broker,
            structured_syslog_kafka_topic,
            structured_syslog_kafka_partitions,
            db_initializer_->GetDbHandler(), gp_.get(), grok_enabled));
    }

    host_ip_ = host_ip;
    if (use_zookeeper) {
        std::string hostname = boost::asio::ip::host_name(error);
        zoo_collector_disc_.reset(new ZookeeperClient(hostname.c_str(),
            zookeeper_server_list.c_str()));
        AddNodeToZooKeeper();
    }
}

VizCollector::VizCollector(EventManager *evm, DbHandlerPtr db_handler,
        Ruleeng *ruleeng, Collector *collector, OpServerProxy *osp) :
    db_initializer_(new DbHandlerInitializer(evm, DbGlobalName(false),
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
    DelNodeFromZoo();
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
    if (structured_syslog_collector_) {
        structured_syslog_collector_->Shutdown();
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
    if (structured_syslog_collector_) {
        structured_syslog_collector_->Initialize();
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

    SessionTableDbInfo stds;
    db_handler->GetSessionTableDbInfo(&stds);
    cds.set_session_table_stats(stds);

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

void VizCollector::AddNodeToZooKeeper() {
    error_code error;
    std::string hostname = boost::asio::ip::host_name(error);
    std::string path = "/" + g_vns_constants.SERVICE_COLLECTOR;
    zoo_collector_disc_->CreateNode(path.c_str(),
                                    hostname.c_str(),
                                    Z_NODE_TYPE_PERSISTENT);

    path = "/" + g_vns_constants.SERVICE_COLLECTOR
                           + "/" + host_ip_;
    if (zoo_collector_disc_->CheckNodeExist(path.c_str())) {
        zoo_collector_disc_->DeleteNode(path.c_str());
    }
    Module::type module = Module::COLLECTOR;
    std::string module_id(g_vns_constants.ModuleNames.find(module)->second);
    NodeType::type node_type =
        g_vns_constants.Module2NodeType.find(module)->second;
    std::string type_name =
        g_vns_constants.NodeTypeNames.find(node_type)->second;
    std::ostringstream instance_str;
    instance_str << getpid();
    std::string instance_id = instance_str.str();
    std::string zoo_val = hostname + ":" + type_name + ":"\
                          + module_id + ":" + instance_id;
    zoo_collector_disc_->CreateNode(path.c_str(),
                                    zoo_val.c_str(),
                                    Z_NODE_TYPE_EPHEMERAL);
}

void VizCollector::DelNodeFromZoo() {
    zoo_collector_disc_->Shutdown();
}
