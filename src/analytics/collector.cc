/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/assign.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include "io/event_manager.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_ctrl_types.h>
#include <sandesh/sandesh_uve_types.h>
#include <sandesh/sandesh_statistics.h>
#include <sandesh/sandesh_session.h>
#include <sandesh/sandesh_connection.h>
#include <sandesh/sandesh_state_machine.h>
#include <sandesh/request_pipeline.h>

#include "collector.h"
#include "viz_collector.h"
#include "ruleeng.h"
#include "viz_sandesh.h"

using std::string;
using std::map;
using std::vector;
using boost::shared_ptr;
using namespace boost::assign;

std::string Collector::prog_name_;
std::string Collector::self_ip_;

bool Collector::task_policy_set_ = false;
const std::string Collector::kDbTask = "analytics::DbHandler";
const std::vector<DbHandler::DbQueueWaterMarkInfo> Collector::kDbQueueWaterMarkInfo =
    boost::assign::tuple_list_of
        (100000, SandeshLevel::SYS_ERR, true)
        (50000, SandeshLevel::SYS_DEBUG, true)
        (75000, SandeshLevel::SYS_DEBUG, false)
        (25000, SandeshLevel::INVALID, false);

Collector::Collector(EventManager *evm, short server_port,
        DbHandler *db_handler, Ruleeng *ruleeng, std::string cassandra_ip,
        unsigned short cassandra_port, int analytics_ttl) :
        SandeshServer(evm),
        db_handler_(db_handler),
        osp_(ruleeng->GetOSP()),
        evm_(evm),
        cb_(boost::bind(&Ruleeng::rule_execute, ruleeng, _1, _2, _3)),
        cassandra_ip_(cassandra_ip),
        cassandra_port_(cassandra_port),
        analytics_ttl_(analytics_ttl),
        db_task_id_(TaskScheduler::GetInstance()->GetTaskId(kDbTask)),
        db_queue_wm_info_(kDbQueueWaterMarkInfo) {

    if (!task_policy_set_) {
        TaskPolicy db_task_policy = boost::assign::list_of
                (TaskExclusion(lifetime_mgr_task_id()));
        TaskScheduler::GetInstance()->SetPolicy(db_task_id_, db_task_policy);
        task_policy_set_ = true;
    }

    SandeshServer::Initialize(server_port);
}

Collector::~Collector() {
}

int Collector::db_task_id() {
    return db_task_id_;
}

void Collector::SessionShutdown() {
    SandeshServer::SessionShutdown();

    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    gen_map_.clear();
}

void Collector::Shutdown() {
    SandeshServer::Shutdown();
}

void Collector::RedisUpdate(bool rsc) {
    LOG(INFO, "RedisUpdate " << rsc);

    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::iterator gen_it = gen_map_.begin();
            gen_it != gen_map_.end(); gen_it++) {
        SandeshGenerator *gen = gen_it->second;
        if (gen->session()) gen->get_state_machine()->ResourceUpdate(rsc);
    }
    return;
}

bool Collector::ReceiveResourceUpdate(SandeshSession *session,
            bool rsc) {
    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        increment_no_session_error();
        LOG(ERROR, __func__ << ": NO VizSession");
        return false;
    }
    if (vsession->gen_) {
        if (!rsc) return true;

        SandeshGenerator *gen = vsession->gen_;
        std::vector<UVETypeInfo> vu;
        std::map<std::string, int32_t> seqReply;
        bool retc = osp_->GetSeq(gen->source(), gen->node_type(),
                        gen->module(), gen->instance_id(), seqReply);
        if (retc) {
            for (map<string,int32_t>::const_iterator it = seqReply.begin();
                    it != seqReply.end(); it++) {
                UVETypeInfo uti;
                uti.set_type_name(it->first);
                uti.set_seq_num(it->second);
                vu.push_back(uti);
            }
            SandeshCtrlServerToClient::Request(vu, retc, "ctrl", vsession->connection());
        } else {
            increment_redis_error();
            LOG(ERROR, "Resource OSP GetSeq FAILED: " << gen->ToString() <<
                " Session: " << vsession->ToString());
            gen->DisconnectSession(vsession);
            return false;
        }

        return true;
    } else {
        increment_no_generator_error();
        LOG(ERROR, __func__ << "Resource State " << rsc <<
                ": SandeshGenerator NOT PRESENT: Session: " << vsession->ToString());
        return false;
    }
}

bool Collector::ReceiveSandeshMsg(SandeshSession *session,
                                  const std::string& cmsg, const std::string& message_type,
                                  const SandeshHeader& header, uint32_t xml_offset, bool rsc) {
    std::string xml_message(cmsg.c_str() + xml_offset,
                            cmsg.size() - xml_offset);

    rand_mutex_.lock();
    boost::uuids::uuid unm(umn_gen_());
    rand_mutex_.unlock();

    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(header, message_type, xml_message, unm));

    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        increment_no_session_error();
        LOG(ERROR, __func__ << ": NO VizSession");
        return false;
    }
    if (vsession->gen_) {
        return vsession->gen_->ReceiveSandeshMsg(vmsgp, rsc);
    } else {
        increment_no_generator_error();
        LOG(ERROR, __func__ << ": Sandesh message " << message_type <<
                ": SandeshGenerator NOT PRESENT: Session: " << vsession->ToString());
        return false;
    }
}


TcpSession* Collector::AllocSession(Socket *socket) {
    VizSession *session = new VizSession(this, socket, AllocConnectionIndex(),
                                         session_writer_task_id(),
                                         session_reader_task_id());
    session->SetBufferSize(kDefaultSessionBufferSize);
    return session;
}

bool Collector::ReceiveSandeshCtrlMsg(SandeshStateMachine *state_machine,
        SandeshSession *session, const Sandesh *sandesh) {
    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        increment_no_session_error();
        LOG(ERROR, "Received Ctrl Message without session " <<
                sandesh->Name());
        return false;
    }
    assert(sandesh);
    const SandeshCtrlClientToServer *snh =
            dynamic_cast<const SandeshCtrlClientToServer *>(sandesh);
    if (!snh) {
        increment_sandesh_type_mismatch_error();
        LOG(ERROR, "Received Ctrl Message with wrong type " <<
                sandesh->Name() << ": Session: " << vsession->ToString());
        return false;
    }
    SandeshGenerator::GeneratorId id(boost::make_tuple(snh->get_source(),
            snh->get_module_name(), snh->get_instance_id_name(),
            snh->get_node_type_name()));
    SandeshGenerator *gen;
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    GeneratorMap::iterator gen_it = gen_map_.find(id);
    if (gen_it == gen_map_.end()) {
        gen = new SandeshGenerator(this, vsession, state_machine, id.get<0>(),
                id.get<1>(), id.get<2>(), id.get<3>());
        gen_map_.insert(id, gen);
    } else {
        // Update the generator if needed
        gen = gen_it->second;
        VizSession *gsession = gen->session();
        if (gsession == NULL) {
            gen->ConnectSession(vsession, state_machine);
        } else {
            increment_session_mismatch_error();
            // Message received on different session. Close both.
            LOG(DEBUG, "Received Ctrl Message: " << gen->ToString()
                   << " On Session:" << vsession->ToString() <<
                   " Current Session:" << gsession->ToString());
            lock.release();
            // Enqueue a close on the state machine on the generator session
            gsession->EnqueueClose();
            return false;
        }
    }
    lock.release();
    LOG(DEBUG, "Received Ctrl Message: " << gen->ToString()
            << " Session:" << vsession->ToString());
    vsession->gen_ = gen;

    std::vector<UVETypeInfo> vu;
    if (snh->get_sucessful_connections() > 1) {
        std::map<std::string, int32_t> seqReply;
        bool retc = osp_->GetSeq(snh->get_source(), snh->get_node_type_name(),
                        snh->get_module_name(), snh->get_instance_id_name(),
                        seqReply);
        if (retc) {
            for (map<string,int32_t>::const_iterator it = seqReply.begin();
                    it != seqReply.end(); it++) {
                UVETypeInfo uti;
                uti.set_type_name(it->first);
                uti.set_seq_num(it->second);
                vu.push_back(uti);
            }
            SandeshCtrlServerToClient::Request(vu, retc, "ctrl", vsession->connection());
        } else {
            increment_redis_error();
            LOG(ERROR, "OSP GetSeq FAILED: " << gen->ToString() <<
                " Session:" << vsession->ToString());
            gen->DisconnectSession(vsession);
            return false;
        }

    } else {
        bool retc = osp_->DeleteUVEs(snh->get_source(), snh->get_node_type_name(),
                        snh->get_module_name(), snh->get_instance_id_name());
        if (retc) {
            SandeshCtrlServerToClient::Request(vu, retc, "ctrl", vsession->connection());
        } else {
            increment_redis_error();
            LOG(ERROR, "OSP DeleteUVEs FAILED: " << gen->ToString() <<
                " Session:" << vsession->ToString());
            gen->DisconnectSession(vsession);
            return false;
        }
    }

    LOG(DEBUG, "Sent good Ctrl Msg: Size " << vu.size() << " " <<
            snh->get_source() << ":" << snh->get_module_name() << ":" <<
            snh->get_instance_id_name() << ":" << snh->get_node_type_name());
    gen->ReceiveSandeshCtrlMsg(snh->get_sucessful_connections());
    return true;
}

void Collector::DisconnectSession(SandeshSession *session) {
    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        increment_no_session_error();
        LOG(ERROR, __func__ << " NO VizSession");
        return;
    }
    SandeshGenerator *gen = vsession->gen_;
    assert(gen);
    LOG(INFO, "Received Disconnect: " << gen->ToString() << " Session:"
            << vsession->ToString());
    gen->DisconnectSession(vsession);
}

void Collector::GetSandeshStats(vector<SandeshMessageStat> &smslist) {
    smslist.clear();
    for (GeneratorMap::iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        SandeshGenerator *gen = gm_it->second;
        // Only send if generator is connected
        VizSession *session = gen->session();
        if (!session) {
            continue;
        }
        vector<SandeshMessageInfo> smi;       
        gen->GetSandeshStats(smi);
        SandeshMessageStat sms;
        sms.set_name(gen->ToString());
        sms.set_msg_info(smi);
        smslist.push_back(sms);
    }
}

void Collector::GetGeneratorSandeshStatsInfo(vector<ModuleServerState> &genlist) {
    genlist.clear();
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::const_iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        const SandeshGenerator * const gen = gm_it->second;
        // Only send if generator is connected
        VizSession *session = gen->session();
        if (!session) {
            continue;
        }
        vector<SandeshStats> ssv;
        gen->GetMessageTypeStats(ssv);
        vector<SandeshLogLevelStats> lsv;
        gen->GetLogLevelStats(lsv);
        vector<SandeshStatsInfo> ssiv;
        SandeshStatsInfo ssi;
        ssi.set_hostname(Sandesh::source());
        ssi.set_msgtype_stats(ssv);
        ssi.set_log_level_stats(lsv);
        ssiv.push_back(ssi);

        ModuleServerState ginfo;
        uint64_t sm_queue_count;
        if (gen->GetSandeshStateMachineQueueCount(sm_queue_count)) {
            ginfo.set_sm_queue_count(sm_queue_count);
        }
	SandeshStateMachineStats sm_stats;
        SandeshGeneratorStats sm_msg_stats;
        if (gen->GetSandeshStateMachineStats(sm_stats, sm_msg_stats)) {
            ginfo.set_sm_stats(sm_stats);
            ginfo.set_sm_msg_stats(sm_msg_stats);
        }
        uint64_t db_queue_count;
        uint64_t db_enqueues;
        std::string db_drop_level;
        uint64_t db_msg_dropped;
        if (gen->GetDbStats(db_queue_count, db_enqueues,
                db_drop_level, db_msg_dropped)) {
            ginfo.set_db_queue_count(db_queue_count);
            ginfo.set_db_enqueues(db_enqueues);
            ginfo.set_db_drop_level(db_drop_level);
            ginfo.set_db_msg_dropped(db_msg_dropped);
        }
        ginfo.set_session_stats(session->GetStats());
        TcpServerSocketStats rx_stats;
        session->GetRxSocketStats(rx_stats);
        ginfo.set_session_rx_socket_stats(rx_stats);
        TcpServerSocketStats tx_stats;
        session->GetTxSocketStats(tx_stats);
        ginfo.set_session_tx_socket_stats(tx_stats);

        ginfo.set_msg_stats(ssiv);
        ginfo.set_name(gen->ToString());
        genlist.push_back(ginfo);
    }
}

void Collector::GetGeneratorSummaryInfo(vector<GeneratorSummaryInfo> &genlist) {
    genlist.clear();
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::const_iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        GeneratorSummaryInfo gsinfo;
        const SandeshGenerator * const gen = gm_it->second;
        ModuleServerState ginfo;
        gen->GetGeneratorInfo(ginfo);
        vector<GeneratorInfo> giv = ginfo.get_generator_info();
        GeneratorInfoAttr gen_attr = giv[0].get_gen_attr();
        if (gen_attr.get_connects() > gen_attr.get_resets()) {
            gsinfo.set_source(gm_it->first.get<0>());
            gsinfo.set_module_id(gm_it->first.get<1>());
            gsinfo.set_instance_id(gm_it->first.get<2>());
            gsinfo.set_node_type(gm_it->first.get<3>());
            gsinfo.set_state(gen->State());
            genlist.push_back(gsinfo);
        }
    }
}

bool Collector::SendRemote(const string& destination, const string& dec_sandesh) {
    std::vector<std::string> dest;
    // destination is of the format "source:module:instance_id:node_type"
    // source/module/instance_id/node_type can be wildcard
    boost::split(dest, destination, boost::is_any_of(":"),
                 boost::token_compress_on);
    if (dest.size() != 4) {
        LOG(ERROR, "Invalid destination " << destination << "." <<
            "Failed to send sandesh request: " << dec_sandesh);
        return false;
    }
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::const_iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        SandeshGenerator::GeneratorId id(gm_it->first);
        if (((dest[0] != "*") && (id.get<0>() != dest[0])) ||
            ((dest[1] != "*") && (id.get<1>() != dest[1])) ||
            ((dest[2] != "*") && (id.get<2>() != dest[2])) ||
            ((dest[3] != "*") && (id.get<3>() != dest[3]))) {
            continue;
        }
        const SandeshGenerator *gen = gm_it->second;
        SandeshSession *session = gen->session();
        if (session) {
            session->EnqueueBuffer((uint8_t *)dec_sandesh.c_str(), dec_sandesh.size());
        } else {
            increment_no_session_error();
            LOG(ERROR, "No connection to " << destination <<
                ". Failed to send sandesh " << dec_sandesh);
        }
    }
    return true;
}

void Collector::SetDbQueueWaterMarkInfo(DbHandler::DbQueueWaterMarkInfo &wm) {
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    GeneratorMap::iterator gen_it = gen_map_.begin();
    for (; gen_it != gen_map_.end(); gen_it++) {
        SandeshGenerator *gen = gen_it->second;
        gen->SetDbQueueWaterMarkInfo(wm);
    }
    db_queue_wm_info_.push_back(wm);
}

void Collector::ResetDbQueueWaterMarkInfo() {
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    GeneratorMap::iterator gen_it = gen_map_.begin();
    for (; gen_it != gen_map_.end(); gen_it++) {
        SandeshGenerator *gen = gen_it->second;
        gen->ResetDbQueueWaterMarkInfo();
    }
    db_queue_wm_info_.clear();
}

void Collector::GetDbQueueWaterMarkInfo(
    std::vector<DbHandler::DbQueueWaterMarkInfo> &wm_info) const {
    wm_info = db_queue_wm_info_;
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
        TcpServerSocketStats rx_socket_stats;
        vsc->Analytics()->GetCollector()->GetRxSocketStats(rx_socket_stats);
        resp->set_rx_socket_stats(rx_socket_stats);
        TcpServerSocketStats tx_socket_stats;
        vsc->Analytics()->GetCollector()->GetTxSocketStats(tx_socket_stats);
        resp->set_tx_socket_stats(tx_socket_stats);
        // Collector statistics
        resp->set_stats(vsc->Analytics()->GetCollector()->GetStats());
        // SandeshGenerator summary info
        vector<GeneratorSummaryInfo> generators;
        vsc->Analytics()->GetCollector()->GetGeneratorSummaryInfo(generators);
        resp->set_generators(generators);
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
    ps.stages_ = list_of(s1);
    RequestPipeline rp(ps);
}

static void SendDbQueueParamsError(std::string estr, std::string context) {
    // SandeshGenerator is required, send error
    DbQueueParamsError *eresp(new DbQueueParamsError);
    eresp->set_context(context);
    eresp->set_error(estr);
    eresp->Response();
}

static void SendDbQueueParamsResponse(Collector *collector,
                                      std::string context) {
    std::vector<DbHandler::DbQueueWaterMarkInfo> wm_info;
    collector->GetDbQueueWaterMarkInfo(wm_info);
    std::vector<DbQueueParams> dbqp_info;
    for (size_t i = 0; i < wm_info.size(); i++) {
        DbHandler::DbQueueWaterMarkInfo wm(wm_info[i]);
        DbQueueParams dbqp;
        dbqp.set_high(boost::get<2>(wm));
        dbqp.set_queue_count(boost::get<0>(wm));
        dbqp.set_drop_level(Sandesh::LevelToString(boost::get<1>(wm)));
        dbqp_info.push_back(dbqp);
    }
    DbQueueParamsResponse *dbqpr(new DbQueueParamsResponse);
    dbqpr->set_info(dbqp_info);
    dbqpr->set_context(context);
    dbqpr->Response();
}

void DbQueueParamsSet::HandleRequest() const {
    if (!(__isset.high && __isset.drop_level && __isset.queue_count)) {
        SendDbQueueParamsError("Please specify all parameters", context());
        return;
    }
    VizSandeshContext *vsc =
            dynamic_cast<VizSandeshContext *>(client_context());
    if (!vsc) {
        SendDbQueueParamsError("Sandesh client context NOT PRESENT",
            context());
        return;
    }
    size_t queue_count(get_queue_count());
    bool high(get_high());
    std::string slevel(get_drop_level());
    SandeshLevel::type dlevel(Sandesh::StringToLevel(slevel));
    DbHandler::DbQueueWaterMarkInfo wm(queue_count, dlevel, high);
    vsc->Analytics()->GetCollector()->SetDbQueueWaterMarkInfo(wm);
    SendDbQueueParamsResponse(vsc->Analytics()->GetCollector(), context());
}

void DbQueueParamsReset::HandleRequest() const {
    VizSandeshContext *vsc =
            dynamic_cast<VizSandeshContext *>(client_context());
    if (!vsc) {
        SendDbQueueParamsError("Sandesh client context NOT PRESENT",
            context());
        return;
    }
    vsc->Analytics()->GetCollector()->ResetDbQueueWaterMarkInfo();
    SendDbQueueParamsResponse(vsc->Analytics()->GetCollector(), context());
}

void DbQueueParamsStatus::HandleRequest() const {
    VizSandeshContext *vsc =
            dynamic_cast<VizSandeshContext *>(client_context());
    if (!vsc) {
        SendDbQueueParamsError("Sandesh client context NOT PRESENT",
            context());
        return;
    }
    SendDbQueueParamsResponse(vsc->Analytics()->GetCollector(), context());
}
