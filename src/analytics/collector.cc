/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/assign.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/array.hpp>
#include <boost/uuid/name_generator.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include <base/connection_info.h>
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
#include <sandesh/sandesh_message_builder.h>
#include <discovery/client/discovery_client.h>
#include <discovery_client_stats_types.h>
#include "collector.h"
#include "viz_collector.h"
#include "viz_sandesh.h"

using std::string;
using std::map;
using std::vector;
using boost::shared_ptr;
using namespace boost::assign;
using std::pair;
using boost::system::error_code;
using process::ConnectionState;
using process::ConnectionType;
using process::ConnectionStatus;


std::string Collector::prog_name_;
std::string Collector::self_ip_;
DiscoveryServiceClient *Collector::ds_client_;

bool Collector::task_policy_set_ = false;
const std::string Collector::kDbTask = "analytics::DbHandler";
const int Collector::kQSizeHighWaterMark = 7 * 1024 * 1024;
const int Collector::kQSizeLowWaterMark  = 3 * 1024 * 1024;

const std::vector<Sandesh::QueueWaterMarkInfo> Collector::kDbQueueWaterMarkInfo =
    boost::assign::tuple_list_of
        (Collector::kQSizeHighWaterMark, SandeshLevel::INVALID, true, true)
        (Collector::kQSizeLowWaterMark, SandeshLevel::INVALID, false, true);
const std::vector<Sandesh::QueueWaterMarkInfo> Collector::kSmQueueWaterMarkInfo =
    boost::assign::tuple_list_of
        (Collector::kQSizeHighWaterMark, SandeshLevel::INVALID, true, true)
        (Collector::kQSizeLowWaterMark, SandeshLevel::INVALID, false, true);

Collector::Collector(EventManager *evm, short server_port,
        DbHandlerPtr db_handler, OpServerProxy *osp, VizCallback cb,
        std::vector<std::string> cassandra_ips,
        std::vector<int> cassandra_ports, const TtlMap& ttl_map,
        const std::string &cassandra_user,
        const std::string &cassandra_password) :
        SandeshServer(evm),
        db_handler_(db_handler),
        osp_(osp),
        evm_(evm),
        cb_(cb),
        cassandra_ips_(cassandra_ips),
        cassandra_ports_(cassandra_ports),
        ttl_map_(ttl_map),
        db_task_id_(TaskScheduler::GetInstance()->GetTaskId(kDbTask)),
        cassandra_user_(cassandra_user),
        cassandra_password_(cassandra_password),
        db_queue_wm_info_(kDbQueueWaterMarkInfo),
        sm_queue_wm_info_(kSmQueueWaterMarkInfo) {

    dbConnStatus_ = ConnectionStatus::INIT;

    if (!task_policy_set_) {
        TaskPolicy db_task_policy = boost::assign::list_of
                (TaskExclusion(lifetime_mgr_task_id()));
        TaskScheduler::GetInstance()->SetPolicy(db_task_id_, db_task_policy);
        task_policy_set_ = true;
    }

    SandeshServer::Initialize(server_port);

    Module::type module = Module::COLLECTOR;
    string module_name = g_vns_constants.ModuleNames.find(module)->second;
    Sandesh::RecordPort("collector", module_name, GetPort());
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
    SandeshGenerator *gen = vsession->generator();
    if (gen) {
        if (!rsc) {
            LOG(ERROR, "Force gen " << gen->ToString() <<
                " to disconnect on redis disconnection");
            gen->DisconnectSession(vsession);
            return false;
        }
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
                                  const SandeshMessage *msg, bool rsc) {
    boost::uuids::uuid unm(umn_gen_());

    VizMsg vmsg(msg, unm);

    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        increment_no_session_error();
        LOG(ERROR, __func__ << ": NO VizSession");
        return false;
    }
    SandeshGenerator *gen = vsession->generator();
    if (gen) {
        return gen->ReceiveSandeshMsg(&vmsg, rsc);
    } else {
        increment_no_generator_error();
        LOG(ERROR, __func__ << ": Sandesh message " << msg->GetMessageType() <<
                ": SandeshGenerator NOT PRESENT: Session: " << vsession->ToString());
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
    if (snh->get_instance_id_name().empty()) {
        LOG(ERROR, "Received Ctrl Message with empty instance id from " <<
            snh->get_source() << ":" << snh->get_module_name());
        return false;
    }
    if (snh->get_node_type_name().empty()) {
        LOG(ERROR, "Received Ctrl Message with empty node type from " <<
            snh->get_source() << ":" << snh->get_module_name());
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
                id.get<1>(), id.get<2>(), id.get<3>(), db_handler_);
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
    LOG(DEBUG, "Received Ctrl Message: " << gen->ToString()
            << " Session:" << vsession->ToString());
    vsession->set_generator(gen);
    lock.release();
    
    std::vector<UVETypeInfo> vu;
    std::map<std::string, int32_t> seqReply;
    bool retc = osp_->GetSeq(snh->get_source(), snh->get_node_type_name(),
                             snh->get_module_name(), snh->get_instance_id_name(),
                             seqReply);
    if (retc) {
        for (map<string,int32_t>::const_iterator it = seqReply.begin();
             it != seqReply.end(); it++) {
            UVETypeInfo uti;
            uti.set_type_name(it->first);
            uti.set_seq_num(0);
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
    SandeshGenerator *gen = vsession->generator();
    assert(gen);
    LOG(INFO, "Received Disconnect: " << gen->ToString() << " Session:"
            << vsession->ToString());
    gen->DisconnectSession(vsession);
}

std::string Collector::DbGlobalName(bool dup) {
    std::string name;
    error_code error;
    if (dup)
        name = boost::asio::ip::host_name(error) + "dup" + ":" + "Global";
    else
        name = boost::asio::ip::host_name(error) + ":" + "Global";

    return name;
}

void Collector::SendGeneratorStatistics() {
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        SandeshGenerator *gen = gm_it->second;
        // Only send if generator is connected
        VizSession *session = gen->session();
        if (!session) {
            continue;
        }
        // Sandesh message info
        gen->SendSandeshMessageStatistics();
    }
}

void Collector::GetGeneratorUVEInfo(vector<ModuleServerState> &genlist) {
    genlist.clear();
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    for (GeneratorMap::const_iterator gm_it = gen_map_.begin();
            gm_it != gen_map_.end(); gm_it++) {
        const SandeshGenerator * const gen = gm_it->second;

        vector<SandeshStats> ssv;
        gen->GetStatistics(&ssv);
        vector<SandeshLogLevelStats> lsv;
        gen->GetStatistics(&lsv);
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
        std::string sm_drop_level;
        if (gen->GetSandeshStateMachineDropLevel(sm_drop_level)) {
            ginfo.set_sm_drop_level(sm_drop_level);
        }
	SandeshStateMachineStats sm_stats;
        SandeshGeneratorBasicStats sm_msg_stats;
        if (gen->GetSandeshStateMachineStats(sm_stats, sm_msg_stats)) {
            ginfo.set_sm_stats(sm_stats);
            ginfo.set_sm_msg_stats(sm_msg_stats);
        }
        // Only send if generator is connected
        VizSession *session = gen->session();
        if (session) {
            ginfo.set_session_stats(session->GetStats());
            SocketIOStats rx_stats;
            session->GetRxSocketStats(&rx_stats);
            ginfo.set_session_rx_socket_stats(rx_stats);
            SocketIOStats tx_stats;
            session->GetTxSocketStats(&tx_stats);
            ginfo.set_session_tx_socket_stats(tx_stats);
        }

        ginfo.set_msg_stats(ssiv);
        ginfo.set_name(gen->ToString());
        genlist.push_back(ginfo);
    }
}

void Collector::GetGeneratorSummaryInfo(vector<GeneratorSummaryInfo> *genlist) {
    genlist->clear();
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
            uint64_t sm_queue_count;
            if (gen->GetSandeshStateMachineQueueCount(sm_queue_count)) {
                gsinfo.set_sm_queue_count(sm_queue_count);
            }
            gsinfo.set_sm_defer(
                gen->IsStateMachineDeferTimerRunning());
            gsinfo.set_sm_defer_time_msec(
                gen->GetStateMachineDeferTimeMSec());
            genlist->push_back(gsinfo);
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
        // GeneratorId is of the format source..module..instance_id..node_type
        if (((dest[0] != "*") && (id.get<0>() != dest[0])) ||
            ((dest[1] != "*") && (id.get<3>() != dest[1])) ||
            ((dest[2] != "*") && (id.get<1>() != dest[2])) ||
            ((dest[3] != "*") && (id.get<2>() != dest[3]))) {
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

void Collector::SetQueueWaterMarkInfo(QueueType::type type,
    Sandesh::QueueWaterMarkInfo &wm) {
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    GeneratorMap::iterator gen_it = gen_map_.begin();
    for (; gen_it != gen_map_.end(); gen_it++) {
        SandeshGenerator *gen = gen_it->second;
        if (type == QueueType::Db) {
            gen->SetDbQueueWaterMarkInfo(wm);
        } else if (type == QueueType::Sm) {
            gen->SetSmQueueWaterMarkInfo(wm);
        }
    }
    if (type == QueueType::Db) {
        db_queue_wm_info_.push_back(wm);
    } else if (type == QueueType::Sm) {
        sm_queue_wm_info_.push_back(wm);
    }
}

void Collector::SetDbQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm) {
    SetQueueWaterMarkInfo(QueueType::Db, wm);
}
    
void Collector::SetSmQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm) {
    SetQueueWaterMarkInfo(QueueType::Sm, wm);
}

void Collector::ResetQueueWaterMarkInfo(QueueType::type type) {
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    GeneratorMap::iterator gen_it = gen_map_.begin();
    for (; gen_it != gen_map_.end(); gen_it++) {
        SandeshGenerator *gen = gen_it->second;
        if (type == QueueType::Db) {
            gen->ResetDbQueueWaterMarkInfo();
        } else if (type == QueueType::Sm) {
            gen->ResetSmQueueWaterMarkInfo();
        } 
    }
    if (type == QueueType::Db) {
        db_queue_wm_info_.clear();
    } else if (type == QueueType::Sm) {
        sm_queue_wm_info_.clear();
    }
}

void Collector::ResetDbQueueWaterMarkInfo() {
    ResetQueueWaterMarkInfo(QueueType::Db);
}
    
void Collector::ResetSmQueueWaterMarkInfo() {
    ResetQueueWaterMarkInfo(QueueType::Sm);
}

void Collector::GetQueueWaterMarkInfo(QueueType::type type,
    std::vector<Sandesh::QueueWaterMarkInfo> &wm_info) const {
    if (type == QueueType::Db) {
        wm_info = db_queue_wm_info_;
    } else if (type == QueueType::Sm) {
        wm_info = sm_queue_wm_info_;
    }
}

void Collector::GetDbQueueWaterMarkInfo(
    std::vector<Sandesh::QueueWaterMarkInfo> &wm_info) const {
    GetQueueWaterMarkInfo(QueueType::Db, wm_info);
}

void Collector::GetSmQueueWaterMarkInfo(
    std::vector<Sandesh::QueueWaterMarkInfo> &wm_info) const {
    GetQueueWaterMarkInfo(QueueType::Sm, wm_info);
}
