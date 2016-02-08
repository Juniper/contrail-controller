/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "viz_collector.h"
#include <boost/asio/ip/host_name.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <iostream>
#include <vector>

#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include "io/event_manager.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_session.h"
#include <sandesh/request_pipeline.h>

#include "ruleeng.h"
#include "protobuf_collector.h"
#include "sflow_collector.h"
#include "ipfix_collector.h"
#include "viz_sandesh.h"

using std::stringstream;
using std::string;
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
            bool use_cql) :
    db_initializer_(new DbHandlerInitializer(evm, DbGlobalName(dup), -1,
        std::string("collector:DbIf"),
        boost::bind(&VizCollector::DbInitializeCb, this),
        cassandra_ips, cassandra_ports, ttl_map, cassandra_user,
        cassandra_password, use_cql)),
    osp_(new OpServerProxy(evm, this, redis_uve_ip, redis_uve_port,
         redis_password, brokers, partitions, kafka_prefix)),
    ruleeng_(new Ruleeng(db_initializer_->GetDbHandler(), osp_.get())),
    collector_(new Collector(evm, listen_port, db_initializer_->GetDbHandler(),
        osp_.get(),
        boost::bind(&Ruleeng::rule_execute, ruleeng_.get(), _1, _2, _3),
        cassandra_ips, cassandra_ports, ttl_map, cassandra_user,
        cassandra_password, use_cql)),
    syslog_listener_(new SyslogListeners(evm,
            boost::bind(&Ruleeng::rule_execute, ruleeng_.get(), _1, _2, _3),
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
    CollectorPublish();
}

void
VizCollector::CollectorPublish()
{
    if (!collector_) return;
    DiscoveryServiceClient *ds_client = Collector::GetCollectorDiscoveryServiceClient();
    if (!ds_client) return;
    string service_name = g_vns_constants.COLLECTOR_DISCOVERY_SERVICE_NAME;
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
            boost::bind(&Ruleeng::rule_execute, ruleeng, _1, _2, _3),
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

void VizCollector::TestDatabaseConnection() {
    if (collector_) {
        collector_->TestDatabaseConnection();
    }
}

void VizCollector::SendDbStatistics() {
    DbHandlerPtr db_handler(db_initializer_->GetDbHandler());
    // DB stats
    std::vector<GenDb::DbTableInfo> vdbti, vstats_dbti;
    GenDb::DbErrors dbe;
    db_handler->GetStats(&vdbti, &dbe, &vstats_dbti);
    CollectorDbStats *snh(COLLECTOR_DB_STATS_CREATE());
    snh->set_name(name_);
    snh->set_table_info(vdbti);
    snh->set_errors(dbe);
    snh->set_statistics_table_info(vstats_dbti);
    cass::cql::DbStats cql_stats;
    if (db_handler->GetCqlStats(&cql_stats)) {
        snh->set_cql_stats(cql_stats);
    }
    COLLECTOR_DB_STATS_SEND_SANDESH(snh);
}

bool VizCollector::GetCqlMetrics(cass::cql::Metrics *metrics) {
    DbHandlerPtr db_handler(db_initializer_->GetDbHandler());
    return db_handler->GetCqlMetrics(metrics);
}

class ShowCollectorServerHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps, int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowCollectorServerReq *req =
            static_cast<const ShowCollectorServerReq *>(ps.snhRequest_.get());
        ShowCollectorServerResp *resp = new ShowCollectorServerResp;
        VizSandeshContext *vsc =
            dynamic_cast<VizSandeshContext *>(req->client_context());
        if (!vsc) {
            LOG(ERROR, __func__ << ": Sandesh client context NOT PRESENT");
            resp->Response();
            return true;
        }
        // Socket statistics
        SocketIOStats rx_socket_stats;
        Collector *collector(vsc->Analytics()->GetCollector());
        collector->GetRxSocketStats(rx_socket_stats);
        resp->set_rx_socket_stats(rx_socket_stats);
        SocketIOStats tx_socket_stats;
        collector->GetTxSocketStats(tx_socket_stats);
        resp->set_tx_socket_stats(tx_socket_stats);
        // Collector statistics
        resp->set_stats(vsc->Analytics()->GetCollector()->GetStats());
        // SandeshGenerator summary info
        std::vector<GeneratorSummaryInfo> generators;
        collector->GetGeneratorSummaryInfo(&generators);
        resp->set_generators(generators);
        resp->set_num_generators(generators.size());
        // CQL metrics if supported
        cass::cql::Metrics cmetrics;
        if (vsc->Analytics()->GetCqlMetrics(&cmetrics)) {
            resp->set_cql_metrics(cmetrics);
        }
        // Send the response
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowCollectorServerReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has single stage to collect neighbor config info
    // and respond to the request
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("collector::ShowCommand");
    s1.cbFn_ = ShowCollectorServerHandler::CallbackS1;
    s1.instances_.push_back(0);
    ps.stages_ = boost::assign::list_of(s1);
    RequestPipeline rp(ps);
}
