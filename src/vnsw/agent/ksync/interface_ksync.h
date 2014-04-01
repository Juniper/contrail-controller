/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_ksync_h
#define vnsw_agent_interface_ksync_h

#include <net/ethernet.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "oper/interface_common.h"
#include "ksync/agent_ksync_types.h"
#include "vr_types.h"

using namespace std;

void KSyncInterfaceCreate(Interface::Type type, const char *if_name,
                          uint32_t vrf_id, uint32_t &ifindex, uint32_t &fd,
                          struct ether_addr &mac);
void KSyncInterfaceDelete(Interface::Type type, const char *if_name,
                          uint32_t vrf_id, uint32_t ifindex, uint32_t fd);
void GetPhyMac(const char *ifname, char *mac);

class Timer;
class InterfaceKSyncObject;

class InterfaceKSyncEntry : public KSyncNetlinkDBEntry {
public:
    InterfaceKSyncEntry(InterfaceKSyncObject *obj, 
                        const InterfaceKSyncEntry *entry, uint32_t index);
    InterfaceKSyncEntry(InterfaceKSyncObject *obj, const Interface *intf);
    virtual ~InterfaceKSyncEntry();

//.de.byte.breaker
#if defined(__linux__)
    const uint8_t *mac() const {return mac_.ether_addr_octet;}
#else
    const uint8_t *mac() const {return mac_.octet;}
#endif
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
    friend class InterfaceKSyncObject;
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    InterfaceKSyncObject *ksync_obj_;
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
    bool ipv4_active_;
    bool l2_active_;
    size_t os_index_;
    int network_id_;
    InetInterface::SubType sub_type_;
    bool ipv4_forwarding_;
    bool layer2_forwarding_;
    uint16_t vlan_id_;
    KSyncEntryPtr parent_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceKSyncEntry);
};

class InterfaceKSyncObject : public KSyncDBObject {
public:
    InterfaceKSyncObject(KSync *parent);
    virtual ~InterfaceKSyncObject();

    KSync *ksync() const { return ksync_; }
    const char *physical_interface_mac() const {return physical_interface_mac_;}

    void Init();
    void InitTest();
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();

private:
    KSync *ksync_;
    char physical_interface_mac_[ETHER_ADDR_LEN];
    int test_mode;
    DISALLOW_COPY_AND_ASSIGN(InterfaceKSyncObject);
};

#endif // vnsw_agent_interface_ksync_h
