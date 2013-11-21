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

#include <sandesh/sandesh_session.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_ctrl_types.h>
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
        db_task_id_(TaskScheduler::GetInstance()->GetTaskId(kDbTask)) {

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
        Generator *gen = gen_it->second;
        if (gen->session()) gen->get_state_machine()->ResourceUpdate(rsc);
    }
    return;
}

bool Collector::ReceiveResourceUpdate(SandeshSession *session,
            bool rsc) {
    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        LOG(ERROR, __func__ << ": NO VizSession");
        return false;
    }
    if (vsession->gen_) {
        if (!rsc) return true;
        
        Generator *gen = vsession->gen_;
        std::vector<UVETypeInfo> vu;
        std::map<std::string, int32_t> seqReply;
        bool retc = osp_->GetSeq(gen->source(), gen->module(), seqReply);
        if (retc) {
            for (map<string,int32_t>::const_iterator it = seqReply.begin();
                    it != seqReply.end(); it++) {
                UVETypeInfo uti;
                uti.set_type_name(it->first);
                uti.set_seq_num(it->second);
                vu.push_back(uti);
            }
            SandeshCtrlServerToClient::Request(vu, retc, "ctrl", vsession->connection());
        }
        if (!retc) {
            gen->set_session(NULL);
            return false;
        }

        return true;
    } else {
        LOG(ERROR, __func__ << "Resource State " << rsc <<
                ": Generator NOT PRESENT: Session: " << vsession->ToString());
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
        LOG(ERROR, __func__ << ": NO VizSession");
        return false;
    }
    if (vsession->gen_) {
        return vsession->gen_->ReceiveSandeshMsg(vmsgp, rsc);
    } else {
        LOG(ERROR, __func__ << ": Sandesh message " << message_type <<
                ": Generator NOT PRESENT: Session: " << vsession->ToString());
        return false;
    }
}

 
TcpSession* Collector::AllocSession(Socket *socket) {
    VizSession *session = new VizSession(this, socket, AllocConnectionIndex(), 
                                         session_writer_task_id(),
                                         session_reader_task_id());
    return session;
}

bool Collector::ReceiveSandeshCtrlMsg(SandeshStateMachine *state_machine,
        SandeshSession *session, const Sandesh *sandesh) {
    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        LOG(ERROR, "Received Ctrl Message without session " <<
                sandesh->Name());
        return false;
    }
    assert(sandesh);
    const SandeshCtrlClientToServer *snh =
            dynamic_cast<const SandeshCtrlClientToServer *>(sandesh);
    if (!snh) {
        LOG(ERROR, "Received Ctrl Message with wrong type " <<
                sandesh->Name() << ": Session: " << vsession->ToString());
        return false;
    }
    Generator::GeneratorId id(std::make_pair(snh->get_source(),
            snh->get_module_name()));
    Generator *gen;
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    GeneratorMap::iterator gen_it = gen_map_.find(id);
    if (gen_it == gen_map_.end()) {
        gen = new Generator(this, vsession, state_machine, id.first,
                id.second);
        gen_map_.insert(id, gen);
    } else {
        // Update the generator if needed
        gen = gen_it->second;
        VizSession *gsession = gen->session();
        if (gsession == NULL) {
            gen->set_session(vsession);
            gen->set_state_machine(state_machine);
        } else {
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
        bool retc = osp_->GetSeq(snh->get_source(), snh->get_module_name(), seqReply);
        if (retc) {
            for (map<string,int32_t>::const_iterator it = seqReply.begin();
                    it != seqReply.end(); it++) {
                UVETypeInfo uti;
                uti.set_type_name(it->first);
                uti.set_seq_num(it->second);
                vu.push_back(uti);
            }
            SandeshCtrlServerToClient::Request(vu, retc, "ctrl", vsession->connection());
        }
        if (!retc) {
            gen->set_session(NULL);
            return false;
        }

    } else {
        bool retc = osp_->DeleteUVEs(snh->get_source(), snh->get_module_name());
        if (retc)
            SandeshCtrlServerToClient::Request(vu, retc, "ctrl", vsession->connection());
        if (!retc) {
            gen->set_session(NULL);
            return false;
        }
    }

    LOG(DEBUG, "Sent good Ctrl Msg: Size " << vu.size() << " " <<
            snh->get_source() << ":" << snh->get_module_name()); 
    gen->ReceiveSandeshCtrlMsg(snh->get_sucessful_connections());
    return true;
}

void Collector::DisconnectSession(SandeshSession *session) {
    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        LOG(ERROR, __func__ << " NO VizSession");
        return;
    }
    Generator *gen = vsession->gen_;
    assert(gen);
    LOG(INFO, "Received Disconnect: " << gen->ToString() << " Session:"
            << vsession->ToString());
    gen->DisconnectSession(vsession);
}

void Collector::GetGeneratorSandeshStatsInfo(vector<ModuleServerState> &genlist) {
    genlist.clear();
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::const_iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        const Generator * const gen = gm_it->second;
        // Only send if generator is connected 
        if (!gen->session()) {
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
        if (gen->GetSandeshStateMachineStats(sm_stats)) {
            ginfo.set_sm_stats(sm_stats);
        }
        uint64_t db_queue_count;
        uint64_t db_enqueues;
        if (gen->GetDbStats(db_queue_count, db_enqueues)) {
            ginfo.set_db_queue_count(db_queue_count);
            ginfo.set_db_enqueues(db_enqueues);
        } 
        ginfo.set_msg_stats(ssiv);
        ginfo.set_name(gen->source() + ":" + gen->module());
        genlist.push_back(ginfo);
    }
}

void Collector::GetGeneratorSummaryInfo(vector<GeneratorSummaryInfo> &genlist) {
    genlist.clear();
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::const_iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        GeneratorSummaryInfo gsinfo;
        const Generator * const gen = gm_it->second;
        ModuleServerState ginfo;
        gen->GetGeneratorInfo(ginfo);
        vector<GeneratorInfo> giv = ginfo.get_generator_info();
        GeneratorInfoAttr gen_attr = giv[0].get_gen_attr();
        if (gen_attr.get_connects() > gen_attr.get_resets()) {
            gsinfo.set_source(gm_it->first.first);
            gsinfo.set_module_id(gm_it->first.second);
            gsinfo.set_state(gen->State());
            genlist.push_back(gsinfo);
        }
    }
}

bool Collector::SendRemote(const string& destination, const string& dec_sandesh) {
    std::vector<std::string> dest;
    // destination is of the format "source:module"
    // source/module can be wildcard
    boost::split(dest, destination, boost::is_any_of(":"),
                 boost::token_compress_on);
    if (dest.size() != 2) {
        LOG(ERROR, "Invalid destination " << destination << "." <<
            "Failed to send sandesh request: " << dec_sandesh);
        return false;
    }
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::const_iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        Generator::GeneratorId id(gm_it->first);
        if (((dest[0] != "*") && (id.first != dest[0])) ||
            ((dest[1] != "*") && (id.second != dest[1]))) {
            continue;
        }
        const Generator *gen = gm_it->second;
        SandeshSession *session = gen->session();
        if (session) {
            session->EnqueueBuffer((uint8_t *)dec_sandesh.c_str(), dec_sandesh.size());
        } else {
            LOG(ERROR, "No connection to " << destination << 
                ". Failed to send sandesh " << dec_sandesh);
        }
    }
    return true;
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
        // Generator summary info
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

