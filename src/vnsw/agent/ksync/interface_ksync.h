/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_intf_ksync_h
#define vnsw_agent_intf_ksync_h

#include <net/ethernet.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "oper/interface_common.h"
#include "vr_types.h"
#include "vr_interface.h"
#include "ksync/agent_ksync_types.h"

using namespace std;

void KSyncInterfaceCreate(Interface::Type type, const char *if_name,
                          uint32_t vrf_id, uint32_t &ifindex, uint32_t &fd,
                          struct ether_addr &mac);
void KSyncInterfaceDelete(Interface::Type type, const char *if_name,
                          uint32_t vrf_id, uint32_t ifindex, uint32_t fd);
void GetPhyMac(const char *ifname, char *mac);

class Timer;
class IntfKSyncObject;

class IntfKSyncEntry : public KSyncNetlinkDBEntry {
public:
    IntfKSyncEntry(const IntfKSyncEntry *entry, uint32_t index, 
                   IntfKSyncObject *obj);
    IntfKSyncEntry(const Interface *intf, IntfKSyncObject *obj);
    virtual ~IntfKSyncEntry();

    const uint8_t *mac() const {return mac_.ether_addr_octet;}
    uint32_t interface_id() const {return interface_id_;}
    const string &interface_name() const {return interface_name_;}
    bool has_service_vlan() const {return has_service_vlan_;}

    KSyncDBObject *GetObject(); 
    virtual bool Sync(DBEntry *e);
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    virtual KSyncEntry *UnresolvedReference();
    void FillObjectLog(sandesh_op::type op, KSyncIntfInfo &info) const;
private:
    friend class IntfKSyncObject;
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    string interface_name_;     // Key
    Interface::Type type_;
    uint32_t interface_id_;
    uint32_t vrf_id_;
    uint32_t fd_;       // FD opened for this
    bool has_service_vlan_;
    struct ether_addr mac_;
    uint32_t ip_;
    bool policy_enabled_;
    string analyzer_name_;
    Interface::MirrorDirection mirror_direction_;
    bool active_;
    size_t os_index_;
    int network_id_;
    InetInterface::SubType sub_type_;
    bool ipv4_forwarding_;
    bool layer2_forwarding_;
    uint16_t vlan_id_;
    KSyncEntryPtr parent_;
    IntfKSyncObject *ksync_obj_;
    DISALLOW_COPY_AND_ASSIGN(IntfKSyncEntry);
};

class IntfKSyncObject : public KSyncDBObject {
public:
    static const int kInterfaceCount = 1000;        // Max interfaces

    IntfKSyncObject(Agent *agent);
    virtual ~IntfKSyncObject();

    Agent *agent() const {return agent_; }
    const char *physical_interface_mac() const {return physical_interface_mac_;}

    void Init();
    void InitTest();
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();

private:
    Agent *agent_;
    char physical_interface_mac_[ETHER_ADDR_LEN];
    int test_mode;
    DISALLOW_COPY_AND_ASSIGN(IntfKSyncObject);
};

// Store kernel interface snapshot
class InterfaceKSnap {
public:
    typedef std::map<std::string, uint32_t> InterfaceKSnapMap;
    typedef std::map<std::string, uint32_t>::iterator InterfaceKSnapIter;
    typedef std::pair<std::string, uint32_t> InterfaceKSnapPair;

    InterfaceKSnap(Agent *agent);
    virtual ~InterfaceKSnap();

    void Init();
    void KernelInterfaceData(vr_interface_req *r);
    bool FindInterfaceKSnapData(std::string &name, uint32_t &ip);
    bool Reset();

private:

    Agent *agent_;
    Timer *timer_;
    tbb::mutex mutex_;
    InterfaceKSnapMap data_map_;
    static const uint32_t timeout_ = 180000; // 3 minutes

    DISALLOW_COPY_AND_ASSIGN(InterfaceKSnap);
};

#endif // vnsw_agent_intf_ksync_h
