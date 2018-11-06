/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OPER_CRYPT_TUNNEL_H_
#define SRC_VNSW_AGENT_OPER_CRYPT_TUNNEL_H_

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <oper/oper_db.h>

extern SandeshTraceBufferPtr CryptTunnelTraceBuf;
#define CRYPT_TUNNEL_TASK_TRACE(obj, ...)\
do {\
    CryptTunnelTask##obj::TraceMsg(CryptTunnelTraceBuf, __FILE__, __LINE__,\
                                   __VA_ARGS__);                        \
} while (false)

class Interface;
class CryptTunnelTable;
class CryptTunnelEntry;
class InstanceTask;
class InstanceTaskExecvp;
class CryptTunnelTask;
class CryptTunnelTaskBase;
struct CryptTunnelEvent;


struct CryptTunnelKey : public AgentKey {
    CryptTunnelKey(IpAddress remote_ip) : AgentKey(), remote_ip_(remote_ip) {} ;
    virtual ~CryptTunnelKey() { };
    IpAddress remote_ip_;
};

struct CryptTunnelConfigData : public AgentData {
    CryptTunnelConfigData(bool vr_crypt) : AgentData(),
            vr_to_vr_crypt_(vr_crypt) { };
    virtual ~CryptTunnelConfigData() { }
    bool vr_to_vr_crypt_;
};

struct CryptTunnelAvailableData : public AgentData {
    CryptTunnelAvailableData(bool available) : AgentData(),
            tunnel_available_(available) { };
    virtual ~CryptTunnelAvailableData() { }
    bool tunnel_available_;
};

class CryptTunnelEntry : AgentRefCount<CryptTunnelEntry>, public AgentDBEntry {
public:
    CryptTunnelEntry(IpAddress remote_ip) : AgentDBEntry(),
            remote_ip_(remote_ip), tunnel_available_(false),
            vr_to_vr_crypt_(false), tunnel_task_(NULL) { };
    virtual ~CryptTunnelEntry() { };

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<CryptTunnelEntry>::GetRefCount();
    }
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(SandeshTraceBufferPtr ptr,
                       AgentLogEvent::type event) const;
    bool GetTunnelAvailable() const { return tunnel_available_;}
    bool GetVRToVRCrypt() const { return vr_to_vr_crypt_;}
    const IpAddress *GetRemoteIp() const { return &remote_ip_;}
    const IpAddress *GetSourceIp() const { return &source_ip_;}
    void SetTunnelAvailable(bool available) { tunnel_available_ = available;}
    void SetVRToVRCrypt(bool crypt) { vr_to_vr_crypt_ = crypt;}
    void UpdateTunnelReference();
    CryptTunnelTaskBase *StartCryptTunnel();
    void StopCryptTunnel();
    void ResyncNH();
    void PostAdd();
private:
    friend class CryptTunnelTable;
    IpAddress remote_ip_;
    IpAddress source_ip_;
    bool tunnel_available_;
    bool vr_to_vr_crypt_;
    CryptTunnelTask *tunnel_task_;
    DISALLOW_COPY_AND_ASSIGN(CryptTunnelEntry);
};

class CryptTunnelTable : public AgentDBTable {
public:
    typedef std::vector<std::string> TunnelEndpointList;

    CryptTunnelTable(Agent *agent, DB *db, const std::string &name);
    virtual ~CryptTunnelTable();

    void set_vr_crypt(bool vr_crypt) { vr_to_vr_crypt_ = vr_crypt;}
    void set_crypt_interface(const Interface *interface) { crypt_interface_ = interface;}
    bool VRToVRCrypt() const { return vr_to_vr_crypt_;};
    void CryptAvailability(const std::string &remote_ip, bool &crypt_traffic, bool &crypt_path_available);

    bool IsCryptPathAvailable(const std::string &remote_ip);
    bool IsCryptTraffic(const std::string &remote_ip);
    void Create(const std::string &remote_ip, bool crypt);
    void Delete(const std::string &remote_ip);
    void Process(DBRequest &req);

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t  Hash(const DBRequestKey *key) const {return 0;};

    CryptTunnelEntry *Find(const std::string &remote_ip);
    virtual DBEntry *Add(const DBRequest *req);
    virtual bool Resync(DBEntry *entry, const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual bool Delete(DBEntry *entry, const DBRequest *req);

    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    static DBTableBase *CreateTable(Agent *agent, DB *db, const std::string &name);
    static CryptTunnelTable *GetInstance() {return crypt_tunnel_table_;};

    bool TunnelEventProcess(CryptTunnelEvent *event);
    void TunnelEventEnqueue(CryptTunnelEvent *event);

private:
    static CryptTunnelTable* crypt_tunnel_table_;
    bool ChangeHandler(CryptTunnelEntry *entry, const DBRequest *req);
    bool vr_to_vr_crypt_;
    InterfaceConstRef crypt_interface_;
    WorkQueue<CryptTunnelEvent *> tunnel_event_queue_;
    DISALLOW_COPY_AND_ASSIGN(CryptTunnelTable);
};


struct CryptTunnelEvent {
public:
    enum EventType {
        MESSAGE_READ = 0,
        TASK_EXIT,
        SET_TUNNEL_ENTRY,
        STOP_TASK,
        EVENT_MAXIMUM
    };

    CryptTunnelEvent(CryptTunnelTaskBase *inst,
                     CryptTunnelEntry *entry, EventType type,
                     const std::string &message);
    virtual ~CryptTunnelEvent();

    CryptTunnelTaskBase *tunnel_task_;
    CryptTunnelEntry *entry_;
    EventType type_;
    std::string message_;
    DISALLOW_COPY_AND_ASSIGN(CryptTunnelEvent);
};


class CryptTunnelTaskBase {
public:
    enum CommandType {
        CREATE_TUNNEL = 0,
        UPDATE_TUNNEL,
        MONITOR_TUNNEL,
        DELETE_TUNNEL
    };
    CryptTunnelTaskBase(CryptTunnelEntry *entry);
    virtual ~CryptTunnelTaskBase();

    virtual bool CreateTunnelTask() = 0;

    // return true it instance is scheduled to destroy
    // when API returns false caller need to assure delete of
    // Crypt Tunnel Instance
    virtual bool DestroyTunnelTask() = 0;
    virtual bool RunTunnelTask(CommandType cmd_type) = 0;
    virtual bool StopTunnelTask() = 0;
    virtual bool UpdateTunnelTask() { return true; }

    // OnRead Callback for Task
    void OnRead(const std::string &data);
    // OnExit Callback for Task
    void OnExit(const boost::system::error_code &ec);
    // Callback to enqueue set tunnel entry
    void SetTunnelEntry(CryptTunnelEntry *entry);
    // Callback to enqueue stop task
    void StopTask(CryptTunnelEntry *service);

    void UpdateTunnel(const CryptTunnelEntry *entry, bool available) const;
    void set_tunnel_entry(CryptTunnelEntry *entry);
    std::string to_string();
    bool active() {return active_;}
    virtual bool IsRunning() const { return true; }
    CryptTunnelEntry *entry() const { return entry_.get(); }
    const std::string &last_update_time() const { return last_update_time_; }

protected:
    friend class CryptTunnelTable;
    // reference to crypt tunnel entry under
    // which this instance is running
    CryptTunnelEntryRef entry_;

   // current status of Crypt tunnel
    bool active_;
    // last update time
    std::string last_update_time_;
    // instance is delete marked
    bool deleted_;

private:
    DISALLOW_COPY_AND_ASSIGN(CryptTunnelTaskBase);
};


// using the instance task infrastructure
class CryptTunnelTask : public CryptTunnelTaskBase {
public:
    typedef InstanceTaskExecvp CryptTunnelProcessTunnel;
    static const std::string kCryptTunnelCmd;

    CryptTunnelTask(CryptTunnelEntry *entry);
    virtual ~CryptTunnelTask();

    virtual bool CreateTunnelTask();
    virtual bool DestroyTunnelTask();
    virtual bool RunTunnelTask(CommandType cmd_type);
    virtual bool StopTunnelTask();
    virtual bool IsRunning() const;

private:
    friend class CryptTunnelTable;
    void UpdateTunnelTaskCommand(CommandType cmd_type);

    // task managing external running script for status
    boost::scoped_ptr<CryptTunnelProcessTunnel> task_;

    DISALLOW_COPY_AND_ASSIGN(CryptTunnelTask);
};

#endif
