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
#include <ksync/ksync_netlink.h>
#include "oper/interface_common.h"
#include "vrouter/ksync/agent_ksync_types.h"
#include "vr_types.h"

using namespace std;

void KSyncInterfaceCreate(Interface::Type type, const char *if_name,
                          uint32_t vrf_id, uint32_t &ifindex, uint32_t &fd,
                          MacAddress &mac);
void KSyncInterfaceDelete(Interface::Type type, const char *if_name,
                          uint32_t vrf_id, uint32_t ifindex, uint32_t fd);
void GetPhyMac(const char *ifname, char *mac);

class Timer;
class InterfaceKSyncObject;

class InterfaceKSyncEntry : public KSyncNetlinkDBEntry {
public:
    static const int kDefaultInterfaceMsgSize = 2048;

    InterfaceKSyncEntry(InterfaceKSyncObject *obj,
                        const InterfaceKSyncEntry *entry, uint32_t index);
    InterfaceKSyncEntry(InterfaceKSyncObject *obj, const Interface *intf);
    virtual ~InterfaceKSyncEntry();

	const MacAddress &mac() const {
        if (parent_.get() == NULL) {
            return mac_;
        } else {
            const InterfaceKSyncEntry *parent =
                        static_cast<const InterfaceKSyncEntry *>(parent_.get());
            return parent->mac();
        }
    }
    const MacAddress &smac() const {return smac_;}

    uint32_t interface_id() const {return interface_id_;}
    const string &interface_name() const {return interface_name_;}
    bool has_service_vlan() const {return has_service_vlan_;}
    bool no_arp() const { return no_arp_; }
    PhysicalInterface::EncapType encap_type() const { return encap_type_; }

    KSyncDBObject *GetObject() const;
    virtual bool Sync(DBEntry *e);
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    virtual KSyncEntry *UnresolvedReference();
    void FillObjectLog(sandesh_op::type op, KSyncIntfInfo &info) const;
    bool drop_new_flows() const {return drop_new_flows_;}
    bool dhcp_enable() const {return dhcp_enable_;}
    bool layer3_forwarding() const {return layer3_forwarding_;}
    bool bridging() const {return bridging_;}

    int MsgLen() { return kDefaultInterfaceMsgSize; }

private:
    friend class InterfaceKSyncObject;
    int Encode(sandesh_op::type op, char *buf, int buf_len);

    string analyzer_name_;
    bool drop_new_flows_;
    bool dhcp_enable_;
    uint32_t fd_;       // FD opened for this
    uint32_t flow_key_nh_id_;
    bool has_service_vlan_;
    uint32_t interface_id_;
    string interface_name_;     // Key
    uint32_t ip_;
    bool hc_active_;
    bool ipv4_active_;
    bool layer3_forwarding_;
    InterfaceKSyncObject *ksync_obj_;
    bool l2_active_;
    bool metadata_l2_active_;
    bool metadata_ip_active_;
    bool bridging_;
    VmInterface::ProxyArpMode proxy_arp_mode_;
    MacAddress mac_;
    MacAddress smac_;
    Interface::MirrorDirection mirror_direction_;
    int network_id_;
    size_t os_index_;
    KSyncEntryPtr parent_;
    bool policy_enabled_;
    InetInterface::SubType sub_type_;
    VmInterface::DeviceType vmi_device_type_;
    VmInterface::VmiType vmi_type_;
    Interface::Type type_;
    uint16_t rx_vlan_id_;
    uint16_t tx_vlan_id_;
    uint32_t vrf_id_;
    bool persistent_;
    PhysicalInterface::SubType subtype_;
    KSyncEntryPtr xconnect_;
    bool no_arp_;
    PhysicalInterface::EncapType encap_type_;
    std::string display_name_;
    Interface::Transport transport_;
    bool flood_unknown_unicast_;
    VmInterface::FatFlowList fat_flow_list_;
    KSyncEntryPtr qos_config_;
    bool learning_enabled_;
    uint32_t isid_;
    uint32_t pbb_cmac_vrf_;
    MacAddress pbb_mac_;
    bool etree_leaf_;
    bool pbb_interface_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceKSyncEntry);
};

class InterfaceKSyncObject : public KSyncDBObject {
public:
    InterfaceKSyncObject(KSync *parent);
    virtual ~InterfaceKSyncObject();

    KSync *ksync() const { return ksync_; }

    void Init();
    void InitTest();
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();
    DBFilterResp DBEntryFilter(const DBEntry *e, const KSyncDBEntry *k);

private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceKSyncObject);
};

#endif // vnsw_agent_interface_ksync_h
