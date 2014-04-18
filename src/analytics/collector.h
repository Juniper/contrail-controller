/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef COLLECTOR_H_
#define COLLECTOR_H_

#include <boost/asio/ip/tcp.hpp>
#include <boost/ptr_container/ptr_map.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#if __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#include <boost/uuid/uuid_generators.hpp>
#if __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic pop
#endif

#include "base/parse_object.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_server.h>
#include <sandesh/sandesh_session.h>

#include "Thrift.h"
#include "viz_constants.h"
#include "generator.h"
#include <string>
#include "collector_uve_types.h"

class DbHandler;
class Ruleeng;
class OpServerProxy;
class EventManager;
class SandeshStateMachine;

class Collector : public SandeshServer {
public:
    const static std::string kDbTask;

    typedef boost::function<bool(const VizMsg*, bool, DbHandler *)> VizCallback;

    Collector(EventManager *evm, short server_port,
              DbHandler *db_handler, Ruleeng *ruleeng,
              std::string cassandra_ip="127.0.0.1", unsigned short cassandra_port=9160, int analytics_ttl=7);
    virtual ~Collector();
    virtual void Shutdown();
    virtual void SessionShutdown();

    virtual bool ReceiveResourceUpdate(SandeshSession *session,
            bool rsc);
    virtual bool ReceiveSandeshMsg(SandeshSession *session,
            const SandeshMessage *msg, bool rsc);
    virtual bool ReceiveSandeshCtrlMsg(SandeshStateMachine *state_machine,
            SandeshSession *session, const Sandesh *sandesh);

    void GetGeneratorSummaryInfo(std::vector<GeneratorSummaryInfo> &genlist);
    void GetGeneratorStats(std::vector<SandeshMessageStat> &smslist,
        std::vector<GeneratorDbStats> &gdbslist);
    void GetGeneratorUVEInfo(std::vector<ModuleServerState> &genlist);
    bool SendRemote(const std::string& destination,
            const std::string &dec_sandesh);

    struct QueueType {
        enum type {
            Db,
            Sm,
        };
    };
    void SetDbQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm);
    void ResetDbQueueWaterMarkInfo();
    void GetDbQueueWaterMarkInfo(
        std::vector<Sandesh::QueueWaterMarkInfo> &wm_info) const;
    void SetSmQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm);
    void ResetSmQueueWaterMarkInfo();
    void GetSmQueueWaterMarkInfo(
        std::vector<Sandesh::QueueWaterMarkInfo> &wm_info) const;
    void GetQueueWaterMarkInfo(QueueType::type type,
        std::vector<Sandesh::QueueWaterMarkInfo> &wm_info) const;

    OpServerProxy * GetOSP() const { return osp_; }
    EventManager * event_manager() const { return evm_; }
    VizCallback ProcessSandeshMsgCb() const { return cb_; }
    void RedisUpdate(bool rsc);

    static const std::string &GetProgramName() { return prog_name_; };
    static void SetProgramName(const char *name) { prog_name_ = name; };
    static std::string GetSelfIp() { return self_ip_; }
    static void SetSelfIp(std::string ip) { self_ip_ = ip; }

    std::string cassandra_ip() { return cassandra_ip_; }
    unsigned short cassandra_port() { return cassandra_port_; }
    int analytics_ttl() { return analytics_ttl_; }
    int db_task_id();
    const CollectorStats &GetStats() const { return stats_; }

    static void SetDiscoveryServiceClient(DiscoveryServiceClient *ds) {
        ds_client_ = ds;
    }

    static DiscoveryServiceClient *GetCollectorDiscoveryServiceClient() {
        return ds_client_;
    }

protected:
    virtual TcpSession *AllocSession(Socket *socket);
    virtual void DisconnectSession(SandeshSession *session);

private:
    void SetQueueWaterMarkInfo(QueueType::type type,
        Sandesh::QueueWaterMarkInfo &wm);
    void ResetQueueWaterMarkInfo(QueueType::type type);

    void inline increment_no_session_error() {
        stats_.no_session_error++;
    }
    void inline increment_no_generator_error() {
        stats_.no_generator_error++;
    }
    void inline increment_session_mismatch_error() {
        stats_.session_mismatch_error++;
    }
    void inline increment_redis_error() {
        stats_.redis_error++;
    }
    void inline increment_sandesh_type_mismatch_error() {
        stats_.sandesh_type_mismatch_error++;
    }

    DbHandler *db_handler_;
    OpServerProxy * const osp_;
    EventManager * const evm_;
    VizCallback cb_;

    std::string cassandra_ip_;
    unsigned short cassandra_port_;
    int analytics_ttl_;
    int db_task_id_;

    // SandeshGenerator map
    typedef boost::ptr_map<SandeshGenerator::GeneratorId, SandeshGenerator> GeneratorMap;
    mutable tbb::mutex gen_map_mutex_;
    GeneratorMap gen_map_;

    // Random generator for UUIDs
    tbb::mutex rand_mutex_;
    boost::uuids::random_generator umn_gen_;
    CollectorStats stats_;
    std::vector<Sandesh::QueueWaterMarkInfo> db_queue_wm_info_;
    std::vector<Sandesh::QueueWaterMarkInfo> sm_queue_wm_info_;
    static std::string prog_name_;
    static std::string self_ip_;
    static bool task_policy_set_;
    static const std::vector<Sandesh::QueueWaterMarkInfo> kDbQueueWaterMarkInfo;
    static const std::vector<Sandesh::QueueWaterMarkInfo> kSmQueueWaterMarkInfo;
    static const int kDefaultSessionBufferSize = 16 * 1024;

    static DiscoveryServiceClient *ds_client_;

    DISALLOW_COPY_AND_ASSIGN(Collector);
};

class VizSession : public SandeshSession {
public:
    VizSession(TcpServer *client, Socket *socket, int task_instance,
            int writer_task_id, int reader_task_id) :
        SandeshSession(client, socket, task_instance, writer_task_id,
                       reader_task_id),
        gen_(NULL) { }
    void set_generator(SandeshGenerator *gen) { gen_ = gen; }
    SandeshGenerator* generator() { return gen_; }
private:
    SandeshGenerator *gen_;
};

#endif /* COLLECTOR_H_ */
