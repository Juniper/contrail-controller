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
} while(false);

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
                           uint8_t ip_proto,
                           const std::string &http_method,
                           const std::string &url_path,
                           uint16_t url_port,
                           const std::string &expected_codes,
                           uint32_t delay, uint32_t timeout,
                           uint32_t max_retries, IFMapNode *ifmap_node) :
        AgentOperDBData(agent, ifmap_node), dest_ip_(dest_ip), name_(name),
        monitor_type_(monitor_type), ip_proto_(ip_proto),
        http_method_(http_method), url_path_(url_path), url_port_(url_port),
        expected_codes_(expected_codes), delay_(delay), timeout_(timeout),
        max_retries_(max_retries) {
    }
    virtual ~HealthCheckServiceData() {}

    IpAddress dest_ip_;
    std::string name_;
    std::string monitor_type_;
    uint8_t ip_proto_;
    std::string http_method_;
    std::string url_path_;
    uint16_t url_port_;
    std::string expected_codes_;
    uint32_t delay_;
    uint32_t timeout_;
    uint32_t max_retries_;
    std::set<boost::uuids::uuid> intf_uuid_list_;
};

struct HealthCheckInstanceEvent {
public:
    enum EventType {
        MESSAGE_READ = 0,
        TASK_EXIT,
        EVENT_MAX
    };

    HealthCheckInstanceEvent(HealthCheckInstance *inst, EventType type,
                             const std::string &message);
    virtual ~HealthCheckInstanceEvent();

    HealthCheckInstance *instance_;
    EventType type_;
    std::string message_;
    DISALLOW_COPY_AND_ASSIGN(HealthCheckInstanceEvent);
};

class HealthCheckInstance {
public:
    typedef InstanceTaskExecvp HeathCheckProcessInstance;
    static const std::string kHealthCheckCmd;

    HealthCheckInstance(HealthCheckService *service,
                        MetaDataIpAllocator *allocator, VmInterface *intf);
    ~HealthCheckInstance();

    void ResyncInterface(HealthCheckService *service);

    bool CreateInstanceTask();

    // return true it instance is scheduled to destroy
    // when API returns false caller need to assure delete of
    // Health Check Instance
    bool DestroyInstanceTask();

    // should be called only after creating task
    void UpdateInstanceTaskCommand();

    void set_service(HealthCheckService *service);

    std::string to_string();

    // OnRead Callback for Task
    void OnRead(InstanceTask *task, const std::string &data);
    // OnExit Callback for Task
    void OnExit(InstanceTask *task, const boost::system::error_code &ec);
    bool active() {return active_;}
    bool IsRunning() const;

    HealthCheckService *service() const { return service_.get(); }
    const MetaDataIp *ip() const { return ip_.get(); }
private:
    friend class HealthCheckService;
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
    // task managing external running script for status
    boost::scoped_ptr<HeathCheckProcessInstance> task_;
    // last update time
    std::string last_update_time_;
    // instance is delete marked
    bool deleted_;

private:
    DISALLOW_COPY_AND_ASSIGN(HealthCheckInstance);
};

class HealthCheckService : AgentRefCount<HealthCheckService>,
                           public AgentOperDBEntry {
public:
    typedef std::map<boost::uuids::uuid, HealthCheckInstance *> InstanceList;

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

    void UpdateInstanceServiceReference();
    void DeleteInstances();

    const boost::uuids::uuid &uuid() const { return uuid_; }
    const std::string &name() const { return name_; }

    uint8_t ip_proto() const { return ip_proto_; }
    uint16_t url_port() const { return url_port_; }
private:
    friend class HealthCheckInstance;
    friend class HealthCheckInstanceEvent;

    const HealthCheckTable *table_;
    boost::uuids::uuid uuid_;
    IpAddress dest_ip_;
    std::string name_;
    // monitor type of service PING/HTTP etc
    std::string monitor_type_;
    // ip_proto derived from monitor_type_
    uint8_t ip_proto_;
    std::string http_method_;
    std::string url_path_;
    // tcp/udp port numbers derived from url
    uint16_t url_port_;
    std::string expected_codes_;
    uint32_t delay_;
    uint32_t timeout_;
    uint32_t max_retries_;
    // List of interfaces associated to this HealthCheck Service
    InstanceList intf_list_;
    DISALLOW_COPY_AND_ASSIGN(HealthCheckService);
};

class HealthCheckTable : public AgentOperDBTable {
public:
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

private:
    WorkQueue<HealthCheckInstanceEvent *> *inst_event_queue_;

    DISALLOW_COPY_AND_ASSIGN(HealthCheckTable);
};

#endif  // SRC_VNSW_AGENT_SERVICES_HEALTH_CHECK_H_
