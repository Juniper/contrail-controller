/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "collector.h"

#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include "io/event_manager.h"

#include <sandesh/sandesh_session.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_ctrl_types.h>
#include <sandesh/sandesh_connection.h>
#include <sandesh/sandesh_state_machine.h>
#include "viz_collector.h"
#include "ruleeng.h"

using std::string;
using std::map;
using std::vector;
using boost::shared_ptr;

std::string Collector::prog_name_;
std::string Collector::self_ip_;

void VizSession::EnqueueClose() {
    SandeshConnection *connection = this->connection();
    if (connection && connection->state_machine()) {
        connection->state_machine()->OnSessionEvent(this,
                                                    TcpSession::CLOSE);
    } else {
        LOG(ERROR, __func__ << ": Session: " << ToString() <<
            ": No state machine");
    }
}

Collector::Collector(EventManager *evm, short server_port,
        DbHandler *db_handler, Ruleeng *ruleeng) :
        SandeshServer(evm),
        db_handler_(db_handler),
        osp_(ruleeng->GetOSP()),
        evm_(evm),
        cb_(boost::bind(&Ruleeng::rule_execute, ruleeng, _1)) {
    SandeshServer::Initialize(server_port);
}

Collector::~Collector() {
}

void Collector::Shutdown() {
    SandeshServer::Shutdown();
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    gen_map_.clear();
}

bool Collector::ReceiveSandeshMsg(SandeshSession *session,
                                  const std::string& cmsg, const std::string& message_type,
                                  const SandeshHeader& header, uint32_t xml_offset) {
    std::string xml_message(cmsg.c_str() + xml_offset,
                            cmsg.size() - xml_offset);

    rand_mutex_.lock();
    boost::uuids::uuid unm(umn_gen_());
    rand_mutex_.unlock();

    boost::shared_ptr<VizMsg> vmsgp(new VizMsg(header, message_type, xml_message, unm));

    db_handler_->MessageTableInsert(vmsgp);

    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        LOG(ERROR, __func__ << ": NO VizSession");
        return false;
    }
    if (vsession->gen_) {
        return vsession->gen_->ReceiveSandeshMsg(vmsgp);
    } else {
        LOG(ERROR, __func__ << ": Sandesh message " << message_type <<
                ": Generator NOT PRESENT: Session: " << vsession->ToString());
        return false;
    }
}

bool Collector::ReceiveMsg(SandeshSession *session, ssm::Message *msg) {
    VizSession *vsession = dynamic_cast<VizSession *>(session);
    if (!vsession) {
        LOG(ERROR, __func__ << ": NO VizSession");
        return false;
    }
    if (vsession->gen_) {
        return vsession->gen_->ReceiveMsg(msg);
    } else {
        LOG(ERROR, __func__ << ": ssm::Message: Generator NOT PRESENT: "
                << "Session: " << vsession->ToString());
        return false;
    }
}

TcpSession* Collector::AllocSession(Socket *socket) {
    VizSession *session = new VizSession(this, socket, AllocConnectionIndex(), 
                                         session_task_id());
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
        ginfo.set_msg_stats(ssiv);
        ginfo.set_name(gen->source() + ":" + gen->module());
        if (ssiv.size()) genlist.push_back(ginfo);
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

void Collector::EnqueueSeqRedisReply(Generator::GeneratorId &id,
        const map<string, int32_t>& typemap) {
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    GeneratorMap::iterator gen_it = gen_map_.find(id);
    if (gen_it == gen_map_.end()) {
        LOG(ERROR, "Received SeqRedisReply for " << id.first << ":" <<
            id.second << " after disconnect");
    } else {
        LOG(DEBUG, "Received SeqRedisReply for " << id.first << ":" <<
            id.second);
        std::auto_ptr<RedisReplyMsg> rmsg(new RedisReplyMsg(typemap));
        Generator *gen = gen_it->second;
        gen->EnqueueRedisMessage(rmsg.release());
    }
}

void Collector::EnqueueDelRedisReply(Generator::GeneratorId &id, bool res) {
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    GeneratorMap::iterator gen_it = gen_map_.find(id);
    if (gen_it == gen_map_.end()) {
        LOG(ERROR, "Received DelRedisReply for " << id.first << ":" <<
            id.second << " after disconnect");
    } else {
        LOG(DEBUG, "Received DelRedisReply for " << id.first << ":" <<
            id.second);
        std::auto_ptr<RedisReplyMsg> rmsg(new RedisReplyMsg(res));
        Generator *gen = gen_it->second;
        gen->EnqueueRedisMessage(rmsg.release());
    }
}
