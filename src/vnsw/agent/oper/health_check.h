/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_SERVICES_HEALTH_CHECK_H_
#define SRC_VNSW_AGENT_SERVICES_HEALTH_CHECK_H_

#include <boost/scoped_ptr.hpp>
#include <cmn/agent.h>
#include <oper_db.h>

extern SandeshTraceBufferPtr HealthCheckTraceBuf;
#define HEALTH_CHECK_TRACE(obj, ...)\
do {\
    HealthCheck##obj::TraceMsg(HealthCheckTraceBuf, __FILE__, __LINE__,\
                               __VA_ARGS__);\
} while (false)

class Interface;
class HealthCheckTable;
class HealthCheckService;
class MetaDataIp;
class MetaDataIpAllocator;
class InstanceTask;
class InstanceTaskExecvp;

///////////////////////////////////////////////////////////////////////////////
// HealthCheck module provides service to monitor status of given service
// on an interface and updates the interface state accordingly to propagate
// information accordingly
///////////////////////////////////////////////////////////////////////////////

struct HealthCheckServiceKey : public AgentOperDBKey {
    HealthCheckServiceKey(const boost::uuids::uuid &id) :
        AgentOperDBKey(), uuid_(id) { }
    HealthCheckServiceKey(const boost::uuids::uuid &id, DBSubOperation sub_op) :
        AgentOperDBKey(sub_op), uuid_(id) { }
    virtual ~HealthCheckServiceKey() { }

    boost::uuids::uuid uuid_;
};

struct HealthCheckServiceData : public AgentOperDBData {
    HealthCheckServiceData(Agent *agent, IpAddress dest_ip,
                           const std::string &name,
                           const std::string &monitor_type,
                           const std::string &stype,
                           uint8_t ip_proto,
                           const std::string &http_method,
                           const std::string &url_path,
                           uint16_t url_port,
                           const std::string &expected_codes,
                           uint32_t delay, uint64_t delay_usecs,
                           uint32_t timeout, uint64_t timeout_usecs,
                           uint32_t max_retries, IFMapNode *ifmap_node) :
        AgentOperDBData(agent, ifmap_node), dest_ip_(dest_ip), name_(name),
        monitor_type_(monitor_type), service_type_(stype), ip_proto_(ip_proto),
        http_method_(http_method), url_path_(url_path), url_port_(url_port),
        expected_codes_(expected_codes), delay_(delay),
        delay_usecs_(delay_usecs), timeout_(timeout),
        timeout_usecs_(timeout_usecs), max_retries_(max_retries) {
    }
    virtual ~HealthCheckServiceData() {}

    IpAddress dest_ip_;
    std::string name_;
    std::string monitor_type_;
    // Service type of HealthCheck segment/end-to-end/link-local
    std::string service_type_;
    uint8_t ip_proto_;
    std::string http_method_;
    std::string url_path_;
    uint16_t url_port_;
    std::string expected_codes_;
    uint32_t delay_;
    uint64_t delay_usecs_;
    uint32_t timeout_;
    uint64_t timeout_usecs_;
    uint32_t max_retries_;
    std::set<boost::uuids::uuid> intf_uuid_list_;
};

struct HealthCheckInstanceEvent {
public:
    enum EventType {
        MESSAGE_READ = 0,
        TASK_EXIT,
        SET_SERVICE,
        STOP_TASK,
        EVENT_MAXIMUM
    };

    HealthCheckInstanceEvent(HealthCheckInstanceBase *inst,
                             HealthCheckService *service, EventType type,
                             const std::string &message);
    virtual ~HealthCheckInstanceEvent();

    HealthCheckInstanceBase *instance_;
    HealthCheckService *service_;
    EventType type_;
    std::string message_;
    DISALLOW_COPY_AND_ASSIGN(HealthCheckInstanceEvent);
};

class HealthCheckInstanceBase {
public:

    HealthCheckInstanceBase(HealthCheckService *service,
                            MetaDataIpAllocator *allocator,
                            VmInterface *intf,
                            bool ignore_status_event);
    virtual ~HealthCheckInstanceBase();

    virtual bool CreateInstanceTask() = 0;

    // return true it instance is scheduled to destroy
    // when API returns false caller need to assure delete of
    // Health Check Instance
    virtual bool DestroyInstanceTask() = 0;
    virtual bool RunInstanceTask() = 0;
    virtual bool StopInstanceTask() = 0;
    virtual bool UpdateInstanceTask() { return true; }

    // OnRead Callback for Task
    void OnRead(const std::string &data);
    // OnExit Callback for Task
    void OnExit(const boost::system::error_code &ec);
    // Callback to enqueue set service
    void SetService(HealthCheckService *service);
    // Callback to enqueue stop task
    void StopTask(HealthCheckService *service);

    virtual void ResyncInterface(const HealthCheckService *service) const;
    void set_service(HealthCheckService *service);
    std::string to_string();
    bool active() {return active_;}
    virtual bool IsRunning() const { return true; }
    HealthCheckService *service() const { return service_.get(); }
    InterfaceRef interface() const { return intf_; }
    MetaDataIp *ip() const { return ip_.get(); }
    const std::string &last_update_time() const { return last_update_time_; }
    bool IsStatusEventIgnored() const { return ignore_status_event_; }
    void set_source_ip(const IpAddress &ip) { source_ip_ = ip; }
    IpAddress get_source_ip() const { return source_ip_; };
    IpAddress source_ip() const;
    IpAddress update_source_ip();
    void set_destination_ip(const IpAddress &ip);
    IpAddress destination_ip() const;
    void EnqueueHealthCheckResync(const HealthCheckService *service,
                                  const VmInterface *itf) const;

protected:
    void EnqueueResync(const HealthCheckService *service, Interface *itf) const;
    friend class HealthCheckTable;
    // reference to health check service under
    // which this instance is running
    HealthCheckServiceRef service_;
    // Interface associated to this HealthCheck Instance
    InterfaceRef intf_;
    // MetaData IP Created for this HealthCheck Instance
    boost::scoped_ptr<MetaDataIp> ip_;
    // current status of HealthCheckInstance
    bool active_;
    // last update time
    std::string last_update_time_;
    // instance is delete marked
    bool deleted_;
    // true if the health check up or down status event has to be ignored
    bool ignore_status_event_;

    // source IP to be used while doing the health check
    IpAddress source_ip_;
    // destination IP for the health check
    IpAddress destination_ip_;

private:
    DISALLOW_COPY_AND_ASSIGN(HealthCheckInstanceBase);
};

// Health check instance using the instance task infrastructure
class HealthCheckInstanceTask : public HealthCheckInstanceBase {
public:
    typedef InstanceTaskExecvp HeathCheckProcessInstance;
    static const std::string kHealthCheckCmd;

    HealthCheckInstanceTask(HealthCheckService *service,
                            MetaDataIpAllocator *allocator, VmInterface *intf,
                            bool ignore_status_event);
    virtual ~HealthCheckInstanceTask();

    virtual bool CreateInstanceTask();
    virtual bool DestroyInstanceTask();
    virtual bool RunInstanceTask();
    virtual bool StopInstanceTask();
    virtual bool IsRunning() const;

private:
    friend class HealthCheckTable;

    void UpdateInstanceTaskCommand();

    // task managing external running script for status
    boost::scoped_ptr<HeathCheckProcessInstance> task_;

    DISALLOW_COPY_AND_ASSIGN(HealthCheckInstanceTask);
};

// Health check instance using the services infrastructure
class HealthCheckInstanceService : public HealthCheckInstanceBase {
public:
    HealthCheckInstanceService(HealthCheckService *service,
                               MetaDataIpAllocator *allocator,
                               VmInterface *intf, VmInterface *other_intf,
                               bool ignore_status_event, bool multi_hop);
    virtual ~HealthCheckInstanceService();

    virtual bool CreateInstanceTask();
    virtual bool DestroyInstanceTask();
    virtual bool RunInstanceTask();
    virtual bool StopInstanceTask();
    virtual bool UpdateInstanceTask();
    virtual void ResyncInterface(const HealthCheckService *service) const;

    bool is_multi_hop() const { return multi_hop_; }

private:
    friend class HealthCheckTable;

    // Other Interface associated to this HealthCheck Instance when
    // HealthCheck service type is "segment"
    InterfaceRef other_intf_;

    // BFD health check can be single hop or multi hop, when started for a
    // BGP flow, make it multi hop
    bool multi_hop_;

    DISALLOW_COPY_AND_ASSIGN(HealthCheckInstanceService);
};

class HealthCheckService : AgentRefCount<HealthCheckService>,
                           public AgentOperDBEntry {
public:
    enum HealthCheckType {
        PING,
        HTTP,
        BFD,
        SEGMENT,
        MAX_HEALTH_CHECK_SERVICES
    };
    typedef std::map<boost::uuids::uuid, HealthCheckInstanceBase *> InstanceList;

    HealthCheckService(const HealthCheckTable *table,
                       const boost::uuids::uuid &id);
    ~HealthCheckService();

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    uint32_t GetRefCount() const {
        return AgentRefCount<HealthCheckService>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *resp, std::string &name) const;

    void PostAdd();
    bool Copy(HealthCheckTable *table, const HealthCheckServiceData *data);

    HealthCheckInstanceBase *StartHealthCheckService(
                             VmInterface *intrface, const IpAddress &source_ip,
                             const IpAddress &destination_ip) {
        // health check status event is ignored
        return StartHealthCheckService(intrface, NULL, source_ip,
                                       destination_ip, true, true);
    }
    void StopHealthCheckService(HealthCheckInstanceBase *instance);

    void UpdateInstanceServiceReference();
    void ResyncHealthCheckInterface(const HealthCheckService *service,
                                    const VmInterface *intf);
    void UpdateInterfaceInstanceServiceReference(const VmInterface *intf);
    void DeleteInstances();

    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &name() const { return name_; }

    uint8_t ip_proto() const { return ip_proto_; }
    uint16_t url_port() const { return url_port_; }
    uint32_t delay() const { return delay_; }
    uint64_t delay_usecs() const { return delay_usecs_; }
    uint32_t timeout() const { return timeout_; }
    uint64_t timeout_usecs() const { return timeout_usecs_; }
    uint32_t max_retries() const { return max_retries_; }
    const std::string &url_path() const { return url_path_; }
    const std::string &monitor_type() const { return monitor_type_; }
    const HealthCheckTable *table() const { return table_; }
    bool IsSegmentHealthCheckService() const;
    HealthCheckType health_check_type() const {
        return health_check_type_;
    }
    IpAddress dest_ip() const { return dest_ip_;}

private:
    friend struct HealthCheckInstanceEvent;

    bool IsInstanceTaskBased() const;
    HealthCheckInstanceBase *StartHealthCheckService(VmInterface *intrface,
                                                     VmInterface *paired_vmi,
                                                     const IpAddress &source_ip,
                                                     const IpAddress &destination_ip,
                                                     bool ignore_status_event,
                                                     bool multi_hop);
    HealthCheckType GetHealthCheckType() const;

    const HealthCheckTable *table_;
    boost::uuids::uuid uuid_;
    IpAddress dest_ip_;
    std::string name_;
    // monitor type of service PING/HTTP/BFD etc
    std::string monitor_type_;
    // Service type of HealthCheck segment/end-to-end/link-local
    std::string service_type_;
    // ip_proto derived from monitor_type_
    uint8_t ip_proto_;
    std::string http_method_;
    std::string url_path_;
    // tcp/udp port numbers derived from url
    uint16_t url_port_;
    std::string expected_codes_;
    uint32_t delay_;
    uint64_t delay_usecs_;
    uint32_t timeout_;
    uint64_t timeout_usecs_;
    uint32_t max_retries_;
    // List of interfaces associated to this HealthCheck Service
    InstanceList intf_list_;
    HealthCheckType health_check_type_;
    DISALLOW_COPY_AND_ASSIGN(HealthCheckService);
};

class HealthCheckTable : public AgentOperDBTable {
public:
    enum HealthCheckServiceAction {
        CREATE_SERVICE,
        DELETE_SERVICE,
        RUN_SERVICE,
        STOP_SERVICE,
        UPDATE_SERVICE
    };
    typedef boost::function<bool(HealthCheckServiceAction,
                                 HealthCheckInstanceService *)>
            HealthCheckServiceCallback;

    HealthCheckTable(Agent *agent, DB *db, const std::string &name);
    virtual ~HealthCheckTable();

    static DBTableBase *CreateTable(Agent *agent, DB *db,
                                    const std::string &name);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBResync(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
            const std::string &context);

    HealthCheckService *Find(const boost::uuids::uuid &u);

    void InstanceEventEnqueue(HealthCheckInstanceEvent *event) const;
    bool InstanceEventProcess(HealthCheckInstanceEvent *event);

    void RegisterHealthCheckCallback(HealthCheckServiceCallback fn,
                                     HealthCheckService::HealthCheckType type) {
        health_check_service_cb_[type] = fn;
    }
    HealthCheckServiceCallback health_check_service_callback(
                               HealthCheckService::HealthCheckType type) const {
        return health_check_service_cb_[type];
    }

private:

    WorkQueue<HealthCheckInstanceEvent *> *inst_event_queue_;
    HealthCheckServiceCallback health_check_service_cb_[HealthCheckService::MAX_HEALTH_CHECK_SERVICES];

    DISALLOW_COPY_AND_ASSIGN(HealthCheckTable);
};

struct HealthCheckResyncInterfaceData : public AgentOperDBData {
       HealthCheckResyncInterfaceData(const Agent *agent, IFMapNode *node,
                                      const VmInterface *intf):
        AgentOperDBData(agent, node), intf_(intf) {}
    const VmInterface *intf_;
};

#endif  // SRC_VNSW_AGENT_SERVICES_HEALTH_CHECK_H_
