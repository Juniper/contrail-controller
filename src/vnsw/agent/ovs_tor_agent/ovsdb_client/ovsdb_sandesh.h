/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_SANDESH_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_SANDESH_H_

#include <oper/agent_sandesh.h>

class AgentSandeshArguments;
namespace OVSDB {
class OvsdbSandeshTask : public Task {
public:
    enum TableType {
        PHYSICAL_SWITCH_TABLE = 1,
        PHYSICAL_PORT_TABLE,
        LOGICAL_SWITCH_TABLE,
        VLAN_PORT_TABLE,
        VRF_TABLE,
        UNICAST_REMOTE_TABLE,
        UNICAST_LOCAL_TABLE,
        MULTICAST_LOCAL_TABLE,
        HA_STALE_DEV_VN_TABLE,
        HA_STALE_L2_ROUTE_TABLE,
        TABLE_MAX,
    };

    enum FilterResp {
        FilterAllow = 1,
        FilterDeny
    };

    static const uint8_t kEntriesPerSandesh = 100;
    static const uint8_t kEntriesPerPage = 100;
    OvsdbSandeshTask(std::string resp_ctx, AgentSandeshArguments &args);
    OvsdbSandeshTask(std::string resp_ctx, const std::string &ip,
                     uint32_t port);

    virtual ~OvsdbSandeshTask();

    bool Run();
    std::string Description() const { return "OvsdbSandeshTask"; }

    std::string EncodeFirstPage();

protected:
    std::string ip_;
    uint32_t port_;

private:
    virtual void EncodeArgs(AgentSandeshArguments &args) {}
    virtual FilterResp Filter(KSyncEntry *entry) { return FilterAllow; }
    virtual void UpdateResp(KSyncEntry *entry, SandeshResponse *resp) = 0;
    virtual SandeshResponse *Alloc() = 0;
    virtual KSyncObject *GetObject(OvsdbClientSession *session) = 0;
    virtual TableType GetTableType() = 0;

    virtual bool NoSessionObject() { return false; }

    void EncodeSendPageReq(uint32_t display_count, uint32_t table_size);

    void SendResponse(bool more);

    SandeshResponse *resp_;
    std::string resp_data_;
    uint32_t first_;
    uint32_t last_;
    uint32_t total_count_;
    bool needs_next_;
    bool error_;
    std::string error_msg_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbSandeshTask);
};

class PhysicalPortSandeshTask : public OvsdbSandeshTask {
public:
    PhysicalPortSandeshTask(std::string resp_ctx, AgentSandeshArguments &args);
    PhysicalPortSandeshTask(std::string resp_ctx, const std::string &ip,
                             uint32_t port, const std::string &name);

    virtual ~PhysicalPortSandeshTask();

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return PHYSICAL_PORT_TABLE;}

    std::string name_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalPortSandeshTask);
};

class LogicalSwitchSandeshTask : public OvsdbSandeshTask {
public:
    LogicalSwitchSandeshTask(std::string resp_ctx, AgentSandeshArguments &args);
    LogicalSwitchSandeshTask(std::string resp_ctx, const std::string &ip,
                             uint32_t port, const std::string &name,
                             uint32_t vxlan_id);

    virtual ~LogicalSwitchSandeshTask();

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return LOGICAL_SWITCH_TABLE;}

    std::string name_;
    uint32_t vxlan_id_;
    DISALLOW_COPY_AND_ASSIGN(LogicalSwitchSandeshTask);
};

class VlanPortBindingSandeshTask : public OvsdbSandeshTask {
public:
    VlanPortBindingSandeshTask(std::string resp_ctx,
                               AgentSandeshArguments &args);
    VlanPortBindingSandeshTask(std::string resp_ctx, const std::string &ip,
                               uint32_t port, const std::string &physical_port);

    virtual ~VlanPortBindingSandeshTask();

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return VLAN_PORT_TABLE;}

    std::string physical_port_;
    DISALLOW_COPY_AND_ASSIGN(VlanPortBindingSandeshTask);
};

class OvsdbVrfSandeshTask : public OvsdbSandeshTask {
public:
    OvsdbVrfSandeshTask(std::string resp_ctx, AgentSandeshArguments &args);
    OvsdbVrfSandeshTask(std::string resp_ctx, const std::string &ip,
                        uint32_t port, const std::string &logical_switch,
                        const std::string &mac);

    virtual ~OvsdbVrfSandeshTask();

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return VRF_TABLE;}

    std::string logical_switch_;
    std::string mac_;
    DISALLOW_COPY_AND_ASSIGN(OvsdbVrfSandeshTask);
};

class UnicastMacRemoteSandeshTask : public OvsdbSandeshTask {
public:
    UnicastMacRemoteSandeshTask(std::string resp_ctx,
                                AgentSandeshArguments &args);
    UnicastMacRemoteSandeshTask(std::string resp_ctx, const std::string &ip,
                                uint32_t port, const std::string &ls,
                                const std::string &mac);

    virtual ~UnicastMacRemoteSandeshTask();

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return UNICAST_REMOTE_TABLE;}

    std::string ls_name_;
    std::string mac_;
    DISALLOW_COPY_AND_ASSIGN(UnicastMacRemoteSandeshTask);
};

class UnicastMacLocalSandeshTask : public OvsdbSandeshTask {
public:
    UnicastMacLocalSandeshTask(std::string resp_ctx,
                               AgentSandeshArguments &args);
    UnicastMacLocalSandeshTask(std::string resp_ctx, const std::string &ip,
                               uint32_t port, const std::string &ls,
                               const std::string &mac);

    virtual ~UnicastMacLocalSandeshTask();

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return UNICAST_LOCAL_TABLE;}

    std::string ls_name_;
    std::string mac_;
    DISALLOW_COPY_AND_ASSIGN(UnicastMacLocalSandeshTask);
};

class MulticastMacLocalSandeshTask : public OvsdbSandeshTask {
public:
    MulticastMacLocalSandeshTask(std::string resp_ctx,
                                 AgentSandeshArguments &args);
    MulticastMacLocalSandeshTask(std::string resp_ctx, const std::string &ip,
                                 uint32_t port, const std::string &ls);

    virtual ~MulticastMacLocalSandeshTask();

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return MULTICAST_LOCAL_TABLE;}

    std::string ls_name_;
    DISALLOW_COPY_AND_ASSIGN(MulticastMacLocalSandeshTask);
};

class HaStaleDevVnSandeshTask : public OvsdbSandeshTask {
public:
    HaStaleDevVnSandeshTask(std::string resp_ctx,
                            AgentSandeshArguments &args);
    HaStaleDevVnSandeshTask(std::string resp_ctx, const std::string &dev_name,
                            const std::string &vn_uuid);

    virtual ~HaStaleDevVnSandeshTask();

    virtual bool NoSessionObject() { return true; }

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return HA_STALE_DEV_VN_TABLE;}

    std::string dev_name_;
    std::string vn_uuid_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleDevVnSandeshTask);
};

class HaStaleL2RouteSandeshTask : public OvsdbSandeshTask {
public:
    HaStaleL2RouteSandeshTask(std::string resp_ctx,
                              AgentSandeshArguments &args);
    HaStaleL2RouteSandeshTask(std::string resp_ctx, const std::string &dev_name,
                              const std::string &vn_uuid,
                              const std::string &mac);

    virtual ~HaStaleL2RouteSandeshTask();

    virtual bool NoSessionObject() { return true; }

private:
    void EncodeArgs(AgentSandeshArguments &args);
    FilterResp Filter(KSyncEntry *entry);
    void UpdateResp(KSyncEntry *kentry, SandeshResponse *resp);
    SandeshResponse *Alloc();
    KSyncObject *GetObject(OvsdbClientSession *session);
    TableType GetTableType() { return HA_STALE_L2_ROUTE_TABLE;}

    std::string dev_name_;
    std::string vn_uuid_;
    std::string mac_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleL2RouteSandeshTask);
};

};

#endif  // SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_OVSDB_SANDESH_H_

