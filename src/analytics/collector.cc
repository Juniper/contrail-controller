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

Collector::Collector(EventManager *evm, short server_port,
        DbHandler *db_handler, Ruleeng *ruleeng) :
        SandeshServer(evm),
        db_handler_(db_handler),
        osp_(ruleeng->GetOSP()),
        evm_(evm),
        cb_(boost::bind(&Ruleeng::rule_execute, ruleeng, _1, _2)) {
    SandeshServer::Initialize(server_port);
}

Collector::~Collector() {
}

void Collector::Shutdown() {
    SandeshServer::Shutdown();
    tbb::mutex::scoped_lock lock(gen_map_mutex_);
    gen_map_.clear();
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

    db_handler_->MessageTableInsert(vmsgp);

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


