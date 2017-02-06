//
//  Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#include <boost/assign/list_of.hpp>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include <analytics/viz_sandesh.h>
#include <analytics/collector.h>
#include <analytics/db_handler.h>
#include <analytics/viz_collector.h>
#include <analytics/collector_uve_types.h>
#include <analytics/analytics_types.h>


static void SendCollectorError(std::string estr, const std::string &context) {
    CollectorError *eresp(new CollectorError);
    eresp->set_context(context);
    eresp->set_error(estr);
    eresp->Response();
}

static Collector* ExtractCollectorFromRequest(SandeshContext *vscontext,
    const std::string &context) {
    VizSandeshContext *vsc = 
            dynamic_cast<VizSandeshContext *>(vscontext);
    if (!vsc) {
        SendCollectorError("Sandesh client context NOT PRESENT",
            context);
        return NULL;
    }
    return vsc->Analytics()->GetCollector();
}

static void SendQueueParamsResponse(Collector::QueueType::type type,
                                    Collector *collector,
                                    const std::string &context) {
    std::vector<Sandesh::QueueWaterMarkInfo> wm_info;
    collector->GetQueueWaterMarkInfo(type, wm_info);
    std::vector<QueueParams> qp_info;
    for (size_t i = 0; i < wm_info.size(); i++) {
        Sandesh::QueueWaterMarkInfo wm(wm_info[i]);
        QueueParams qp;
        qp.set_high(boost::get<2>(wm));
        qp.set_queue_count(boost::get<0>(wm));
        qp.set_drop_level(Sandesh::LevelToString(boost::get<1>(wm)));
        qp_info.push_back(qp);
    }
    QueueParamsResponse *qpr(new QueueParamsResponse);
    qpr->set_info(qp_info);
    qpr->set_context(context);
    qpr->Response();
}

void DbQueueParamsSet::HandleRequest() const {
    if (!(__isset.high && __isset.drop_level && __isset.queue_count)) {
        SendCollectorError("Please specify all parameters", context());
        return;
    }
    Collector *collector = ExtractCollectorFromRequest(client_context(),
        context());
    size_t queue_count(get_queue_count());
    bool high(get_high());
    std::string slevel(get_drop_level());
    SandeshLevel::type dlevel(Sandesh::StringToLevel(slevel));
    Sandesh::QueueWaterMarkInfo wm(queue_count, dlevel, high);
    collector->SetDbQueueWaterMarkInfo(wm);
    SendQueueParamsResponse(Collector::QueueType::Db, collector, context());
}

void SmQueueParamsSet::HandleRequest() const {
    if (!(__isset.high && __isset.drop_level && __isset.queue_count)) {
        SendCollectorError("Please specify all parameters", context());
        return;
    }
    Collector *collector = ExtractCollectorFromRequest(client_context(),
        context());
    size_t queue_count(get_queue_count());
    bool high(get_high());
    std::string slevel(get_drop_level());
    SandeshLevel::type dlevel(Sandesh::StringToLevel(slevel));
    Sandesh::QueueWaterMarkInfo wm(queue_count, dlevel, high);
    collector->SetSmQueueWaterMarkInfo(wm);
    SendQueueParamsResponse(Collector::QueueType::Sm, collector, context());
}

void DbQueueParamsReset::HandleRequest() const {
    Collector *collector = ExtractCollectorFromRequest(client_context(),
        context());
    collector->ResetDbQueueWaterMarkInfo();
    SendQueueParamsResponse(Collector::QueueType::Db, collector, context());
}

void SmQueueParamsReset::HandleRequest() const {
    Collector *collector = ExtractCollectorFromRequest(client_context(),
        context());
    collector->ResetSmQueueWaterMarkInfo();
    SendQueueParamsResponse(Collector::QueueType::Sm, collector, context());
}

void DbQueueParamsStatus::HandleRequest() const {
    Collector *collector = ExtractCollectorFromRequest(client_context(),
        context());
    SendQueueParamsResponse(Collector::QueueType::Db, collector, context());
}

void SmQueueParamsStatus::HandleRequest() const {
    Collector *collector = ExtractCollectorFromRequest(client_context(),
        context());
    SendQueueParamsResponse(Collector::QueueType::Sm, collector, context()); 
}

static void SendFlowCollectionStatusResponse(std::string context) {
    FlowCollectionStatusResponse *fcsr(new FlowCollectionStatusResponse);
    fcsr->set_disable(Sandesh::IsFlowCollectionDisabled());
    fcsr->set_context(context);
    fcsr->Response();
}

void DisableFlowCollectionRequest::HandleRequest() const {
    if (__isset.disable) {
        Sandesh::DisableFlowCollection(get_disable());
    }
    // Send response
    SendFlowCollectionStatusResponse(context());
}

void FlowCollectionStatusRequest::HandleRequest() const {
    // Send response
    SendFlowCollectionStatusResponse(context());
}

static DbHandlerPtr ExtractDbHandlerFromRequest(SandeshContext *vscontext,
    const std::string &context) {
    VizSandeshContext *vsc =
            dynamic_cast<VizSandeshContext *>(vscontext);
    if (!vsc) {
        SendCollectorError("Sandesh client context NOT PRESENT",
            context);
        return DbHandlerPtr();
    }
    return vsc->Analytics()->GetDbHandler();
}

static void SendDatabaseWritesStatusResponse(SandeshContext *vscontext, std::string context) {
    DbHandlerPtr dbh(ExtractDbHandlerFromRequest(vscontext, context));
    DatabaseWritesStatusResponse *dwsr(new DatabaseWritesStatusResponse);
    dwsr->set_disable_all(dbh->IsAllWritesDisabled());
    dwsr->set_disable_statistics(dbh->IsStatisticsWritesDisabled());
    dwsr->set_disable_messages(dbh->IsMessagesWritesDisabled());
    dwsr->set_disable_messages_keyword(dbh->IsMessagesKeywordWritesDisabled());
    dwsr->set_disable_flows(Sandesh::IsFlowCollectionDisabled());
    dwsr->set_context(context);
    dwsr->Response();
}

void DisableDatabaseWritesRequest::HandleRequest() const {
    DbHandlerPtr dbh(ExtractDbHandlerFromRequest(client_context(), context()));
    if (__isset.disable_all) {
        dbh->DisableAllWrites(get_disable_all());
    }
    if (__isset.disable_statistics) {
        dbh->DisableStatisticsWrites(get_disable_statistics());
    }
    if (__isset.disable_messages) {
        dbh->DisableMessagesWrites(get_disable_messages());
    }
    if (__isset.disable_messages_keyword) {
        dbh->DisableMessagesKeywordWrites(get_disable_messages_keyword());
    }
    if (__isset.disable_flows) {
        Sandesh::DisableFlowCollection(get_disable_flows());
    }
    // Send response
    SendDatabaseWritesStatusResponse(client_context(), context());
}

void DatabaseWritesStatusRequest::HandleRequest() const {
    // Send response
    SendDatabaseWritesStatusResponse(client_context(), context());
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
        collector->GetRxSocketStats(&rx_socket_stats);
        resp->set_rx_socket_stats(rx_socket_stats);
        SocketIOStats tx_socket_stats;
        collector->GetTxSocketStats(&tx_socket_stats);
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
        // Get cumulative CollectorDbStats
        std::vector<GenDb::DbTableInfo> vdbti, vstats_dbti;
        GenDb::DbErrors dbe;
        DbHandlerPtr db_handler(vsc->Analytics()->GetDbHandler());
        db_handler->GetCumulativeStats(&vdbti, &dbe, &vstats_dbti);
        resp->set_table_info(vdbti);
        resp->set_errors(dbe);
        resp->set_statistics_table_info(vstats_dbti);
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
