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
    IntfKSyncEntry(const IntfKSyncEntry *entry, uint32_t index);
    IntfKSyncEntry(const Interface *intf);

    virtual ~IntfKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const {
        const IntfKSyncEntry &entry = static_cast<const IntfKSyncEntry &>(rhs);
        return ifname_ < entry.ifname_;
    };

    virtual std::string ToString() const;

    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);

    KSyncDBObject *GetObject(); 
    const uint8_t *GetMac() {return mac_.ether_addr_octet;};
    uint32_t GetIpAddress() {return ip_;};
    uint32_t id() const {return intf_id_;}
    const string &GetName() const {return ifname_;};
    void FillObjectLog(sandesh_op::type op, KSyncIntfInfo &info);
    bool HasServiceVlan() const {return has_service_vlan_;};
    int GetNetworkId() const {return network_id_;};

private:
    friend class IntfKSyncObject;
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    string ifname_;     // Key
    Interface::Type type_;
    uint32_t intf_id_;
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
    VirtualHostInterface::SubType sub_type_;
    bool ipv4_forwarding_;
    bool layer2_forwarding_;
    uint16_t vlan_id_;
    KSyncEntryPtr parent_;
    DISALLOW_COPY_AND_ASSIGN(IntfKSyncEntry);
};

class IntfKSyncObject : public KSyncDBObject {
public:
    static const int kInterfaceCount = 1000;        // Max interfaces

    IntfKSyncObject(DBTableBase *table) : 
        KSyncDBObject(table, kInterfaceCount), vnsw_if_mac(), test_mode() {};
    virtual ~IntfKSyncObject() {};

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const IntfKSyncEntry *intf = static_cast<const IntfKSyncEntry *>(entry);
        IntfKSyncEntry *ksync = new IntfKSyncEntry(intf, index);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const Interface *intf = static_cast<const Interface *>(e);
        IntfKSyncEntry *key = NULL;

        switch (intf->type()) {
        case Interface::PHYSICAL:
        case Interface::VM_INTERFACE:
        case Interface::PACKET:
        case Interface::VIRTUAL_HOST:
            key = new IntfKSyncEntry(intf);
            break;

        default:
            assert(0);
            break;
        }
        return static_cast<KSyncEntry *>(key);
    }

    static void Init(InterfaceTable *table);
    static void InitTest(InterfaceTable *table);
    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }
 
    static IntfKSyncObject *GetKSyncObject() {return singleton_;};
    const char *PhysicalIntfMac() const {return vnsw_if_mac;};
    bool GetTestMode() const {return test_mode;};
private:
    static IntfKSyncObject *singleton_;
    char vnsw_if_mac[ETHER_ADDR_LEN];
    int test_mode;
    DISALLOW_COPY_AND_ASSIGN(IntfKSyncObject);
};

// Store kernel interface snapshot
class InterfaceKSnap {
public:
    typedef std::map<std::string, uint32_t> InterfaceKSnapMap;
    typedef std::map<std::string, uint32_t>::iterator InterfaceKSnapIter;
    typedef std::pair<std::string, uint32_t> InterfaceKSnapPair;

    static void Init();
    static void Shutdown();
    static InterfaceKSnap *GetInstance() { return singleton_; }

    virtual ~InterfaceKSnap();
    void KernelInterfaceData(vr_interface_req *r);
    bool FindInterfaceKSnapData(std::string &name, uint32_t &ip);
    bool Reset();

private:
    InterfaceKSnap();

    Timer *timer_;
    tbb::mutex mutex_;
    InterfaceKSnapMap data_map_;
    static const uint32_t timeout_ = 180000; // 3 minutes
    static InterfaceKSnap *singleton_;

    DISALLOW_COPY_AND_ASSIGN(InterfaceKSnap);
};

#endif // vnsw_agent_intf_ksync_h
