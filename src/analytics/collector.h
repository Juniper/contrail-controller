/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef COLLECTOR_H_
#define COLLECTOR_H_

#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "base/parse_object.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_server.h>
#include <sandesh/sandesh_session.h>

#include "Thrift.h"
#include "viz_constants.h"
#include "generator.h"
#include <string>

class DbHandler;
class Ruleeng;
class OpServerProxy;
class EventManager;
class SandeshStateMachine;

class Collector : public SandeshServer {
public:
    typedef boost::function<bool(const boost::shared_ptr<VizMsg>)> VizCallback;

    Collector(EventManager *evm, short server_port,
              DbHandler *db_handler, Ruleeng *ruleeng);
    virtual ~Collector();
    virtual void Shutdown();

    virtual bool ReceiveSandeshMsg(SandeshSession *session,
                           const std::string &cmsg, const std::string &message_type,
                           const SandeshHeader& header, uint32_t xml_offset);
    virtual bool ReceiveSandeshCtrlMsg(SandeshStateMachine *state_machine,
            SandeshSession *session, const Sandesh *sandesh);
    virtual bool ReceiveMsg(SandeshSession *session, ssm::Message *msg);

    void GetGeneratorSummaryInfo(std::vector<GeneratorSummaryInfo> &genlist);
    void GetGeneratorSandeshStatsInfo(std::vector<ModuleServerState> &genlist);
    bool SendRemote(const std::string& destination,
            const std::string &dec_sandesh);
    void EnqueueSeqRedisReply(Generator::GeneratorId &id,
            const std::map<std::string, int32_t> &typemap);
    void EnqueueDelRedisReply(Generator::GeneratorId &id, bool res);

    OpServerProxy * GetOSP() const { return osp_; }
    EventManager * event_manager() const { return evm_; }
    VizCallback ProcessSandeshMsgCb() const { return cb_; }

    static const std::string &GetProgramName() { return prog_name_; };
    static void SetProgramName(const char *name) { prog_name_ = name; };
    static std::string GetSelfIp() { return self_ip_; }
    static void SetSelfIp(std::string ip) { self_ip_ = ip; }

protected:
    virtual TcpSession *AllocSession(Socket *socket);
    virtual void DisconnectSession(SandeshSession *session);

private:
    DbHandler *db_handler_;
    OpServerProxy * const osp_;
    EventManager * const evm_;
    VizCallback cb_;

    // Generator map
    typedef boost::ptr_map<Generator::GeneratorId, Generator> GeneratorMap;
    tbb::mutex gen_map_mutex_;
    GeneratorMap gen_map_;

    // Random generator for UUIDs
    tbb::mutex rand_mutex_;
    boost::uuids::random_generator umn_gen_;
    static std::string prog_name_;
    static std::string self_ip_;

    DISALLOW_COPY_AND_ASSIGN(Collector);
};

class VizSession : public SandeshSession {
public:
    Generator *gen_;
    VizSession(TcpServer *client, Socket *socket, int task_instance,
            int task_id) :
        SandeshSession(client, socket, task_instance, task_id),
        gen_(NULL) { }
};

#endif /* COLLECTOR_H_ */
