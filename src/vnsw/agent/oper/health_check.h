/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_SERVICES_HEALTH_CHECK_H_
#define SRC_VNSW_AGENT_SERVICES_HEALTH_CHECK_H_

#include <cmn/agent.h>
#include <oper_db.h>

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
                           const std::string &http_method,
                           const std::string &url_path,
                           const std::string &expected_codes,
                           uint32_t delay,
                           uint32_t timeout, IFMapNode *ifmap_node) :
        AgentOperDBData(agent, ifmap_node), dest_ip_(dest_ip), name_(name),
        monitor_type_(monitor_type), http_method_(http_method),
        url_path_(url_path), expected_codes_(expected_codes), delay_(delay),
        timeout_(timeout) {
    }
    virtual ~HealthCheckServiceData() {}

    IpAddress dest_ip_;
    std::string name_;
    std::string monitor_type_;
    std::string http_method_;
    std::string url_path_;
    std::string expected_codes_;
    uint32_t delay_;
    uint32_t timeout_;
    std::set<boost::uuids::uuid> intf_uuid_list_;
};

struct HealthCheckInstance {
    static const std::string kHealthCheckCmd;
    HealthCheckInstance(HealthCheckService *service,
                        MetaDataIpAllocator *allocator, VmInterface *intf);
    ~HealthCheckInstance();

    void ResyncInterface();

    bool CreateInstanceTask();
    void DestroyInstanceTask();

    // OnRead Callback for Task
    void OnRead(InstanceTask *task, const std::string data);
    // OnExit Callback for Task
    void OnExit(InstanceTask *task, const boost::system::error_code &ec);
    bool active() {return active_;}

    // service under which this instance is running
    HealthCheckService *service_;
    // Interface associated to this HealthCheck Instance
    InterfaceRef intf_;
    // MetaData IP Created for this HealthCheck Instance
    MetaDataIp *ip_;
    // current status of HealthCheckInstance
    tbb::atomic<bool> active_;
    // task managing external running script for status
    InstanceTaskExecvp *task_;
};

class HealthCheckService : public AgentOperDBEntry {
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
        return 0;
    }

    bool DBEntrySandesh(Sandesh *resp, std::string &name) const;

    bool Copy(HealthCheckTable *table, const HealthCheckServiceData *data);
    const boost::uuids::uuid &uuid() const { return uuid_; }

private:
    friend class HealthCheckInstance;

    const HealthCheckTable *table_;
    boost::uuids::uuid uuid_;
    IpAddress dest_ip_;
    std::string name_;
    // monitor type of service PING/HTTP etc
    std::string monitor_type_;
    std::string http_method_;
    std::string url_path_;
    std::string expected_codes_;
    uint32_t delay_;
    uint32_t timeout_;
    // List of interfaces associated to this HealthCheck Service
    InstanceList intf_list_;
    DISALLOW_COPY_AND_ASSIGN(HealthCheckService);
};

class HealthCheckTable : public AgentOperDBTable {
public:
    HealthCheckTable(DB *db, const std::string &name);
    virtual ~HealthCheckTable();

    static DBTableBase *CreateTable(DB *db, const std::string &name);

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

private:
    static HealthCheckTable *health_check_table_;

    DISALLOW_COPY_AND_ASSIGN(HealthCheckTable);
};

#endif  // SRC_VNSW_AGENT_SERVICES_HEALTH_CHECK_H_
