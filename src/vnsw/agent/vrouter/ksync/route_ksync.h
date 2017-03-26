/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_ksync_h
#define vnsw_agent_route_ksync_h

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <base/lifetime.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include "oper/nexthop.h"
#include "oper/route_common.h"
#include "oper/agent_route_walker.h"
#include "vrouter/ksync/agent_ksync_types.h"
#include "vrouter/ksync/nexthop_ksync.h"

class RouteKSyncObject;

class RouteKSyncEntry : public KSyncNetlinkDBEntry {
public:
    RouteKSyncEntry(RouteKSyncObject* obj, const RouteKSyncEntry *entry, 
                    uint32_t index);
    RouteKSyncEntry(RouteKSyncObject* obj, const AgentRoute *route); 
    virtual ~RouteKSyncEntry();

    uint32_t prefix_len() const { return prefix_len_; }
    uint32_t label() const { return label_; }
    bool proxy_arp() const { return proxy_arp_; }
    bool flood() const { return flood_; }
    bool flood_dhcp() const { return flood_dhcp_; }
    bool wait_for_traffic() const { return wait_for_traffic_; }
    MacAddress mac() const { return mac_; }
    NHKSyncEntry* nh() const { 
        return static_cast<NHKSyncEntry *>(nh_.get());
    }
    void set_prefix_len(uint32_t len) { prefix_len_ = len; }
    void set_ip(IpAddress addr) { addr_ = addr; }
    KSyncDBObject *GetObject() const;

    void FillObjectLog(sandesh_op::type op, KSyncRouteInfo &info) const;
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);

    bool BuildArpFlags(const DBEntry *rt, const AgentPath *path,
                       const MacAddress &mac);
    uint8_t CopyReplacementData(NHKSyncEntry *nexthop, RouteKSyncEntry *new_rt);
private:
    int Encode(sandesh_op::type op, uint8_t replace_plen,
               char *buf, int buf_len);
    int DeleteInternal(NHKSyncEntry *nexthop, RouteKSyncEntry *new_rt,
                       char *buf, int buf_len);
    bool UcIsLess(const KSyncEntry &rhs) const;
    bool McIsLess(const KSyncEntry &rhs) const;
    bool EvpnIsLess(const KSyncEntry &rhs) const;
    bool L2IsLess(const KSyncEntry &rhs) const;
    const NextHop *GetActiveNextHop(const AgentRoute *route) const;
    const AgentPath *GetActivePath(const AgentRoute *route) const;

    RouteKSyncObject* ksync_obj_;
    Agent::RouteTableType rt_type_;
    uint32_t vrf_id_;
    IpAddress addr_;
    IpAddress src_addr_;
    MacAddress mac_;
    uint32_t prefix_len_;
    KSyncEntryPtr nh_;
    uint32_t label_;
    uint8_t type_;
    bool proxy_arp_;
    bool flood_dhcp_;
    string address_string_;
    TunnelType::Type tunnel_type_;
    bool wait_for_traffic_;
    bool local_vm_peer_route_;
    bool flood_;
    uint32_t ethernet_tag_;
    bool layer2_control_word_;
    //////////////////////////////////////////////////////////////////
    // NOTE: Please update CopyReplacmenetData when any new field is added
    // here
    //////////////////////////////////////////////////////////////////
    DISALLOW_COPY_AND_ASSIGN(RouteKSyncEntry);
};

class RouteKSyncObject : public KSyncDBObject {
public:
    struct VrfState : DBState {
        VrfState() : DBState(), seen_(false),
        created_(UTCTimestampUsec()) {};
        bool seen_;
        uint64_t created_;
    };

    RouteKSyncObject(KSync *ksync, AgentRouteTable *rt_table);
    virtual ~RouteKSyncObject();

    KSync *ksync() const { return ksync_; }

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void ManagedDelete();
    void Unregister();
    virtual void EmptyTable();
    DBFilterResp DBEntryFilter(const DBEntry *entry, const KSyncDBEntry *ksync);

private:
    KSync *ksync_;
    bool marked_delete_;
    AgentRouteTable *rt_table_;
    LifetimeRef<RouteKSyncObject> table_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(RouteKSyncObject);
};

struct MacBinding {
    typedef std::map<const MacAddress,
                     PathPreference> MacPreferenceMap;
    typedef std::pair<const MacAddress,
                      PathPreference> MacPreferencePair;

    MacBinding(const MacBinding &mac_binding):
        mac_preference_map_(mac_binding.mac_preference_map_) {}

    MacBinding(const MacAddress &mac, const PathPreference &pref) {
        mac_preference_map_[mac] = pref;
    }

    const MacAddress& get_mac() const {
        const MacAddress *mac = &MacAddress::ZeroMac();
        PathPreference::Preference pref = PathPreference::INVALID;
        for (MacPreferenceMap::const_iterator it = mac_preference_map_.begin();
                it != mac_preference_map_.end(); it++) {
            if (*mac == MacAddress::ZeroMac() || pref < it->second.preference()) {
                mac = &(it->first);
                pref = it->second.preference();
            }
        }
        return *mac;
    }

    void reset_mac(const MacAddress &mac) {
        mac_preference_map_.erase(mac);
    }

    bool can_erase() {
        if (mac_preference_map_.size() == 0) {
            return true;
        }
        return false;
    }

    void set_mac(const PathPreference &pref,
                 const MacAddress &mac) {
        mac_preference_map_[mac] = pref;
    }

    bool WaitForTraffic() const {
        for (MacPreferenceMap::const_iterator it = mac_preference_map_.begin();
                it != mac_preference_map_.end(); it++) {
            if (it->second.wait_for_traffic() == true) {
                return true;
            }
        }
        return false;
    }

private:
    MacPreferenceMap mac_preference_map_;
};

class KSyncRouteWalker;
class VrfKSyncObject {
public:
    // Table to maintain IP - MAC binding. Used to stitch MAC to inet routes
    typedef std::map<IpAddress, MacBinding> IpToMacBinding;

    struct VrfState : DBState {
        VrfState(Agent *agent);
        bool seen_;
        RouteKSyncObject *inet4_uc_route_table_;
        RouteKSyncObject *inet4_mc_route_table_;
        RouteKSyncObject *inet6_uc_route_table_;
        RouteKSyncObject *bridge_route_table_;
        IpToMacBinding  ip_mac_binding_;
        DBTableBase::ListenerId evpn_rt_table_listener_id_;
        KSyncRouteWalker *ksync_route_walker_;
    };

    VrfKSyncObject(KSync *ksync);
    virtual ~VrfKSyncObject();

    KSync *ksync() const { return ksync_; }

    void RegisterDBClients();
    void Shutdown();
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);

    void EvpnRouteTableNotify(DBTablePartBase *partition, DBEntryBase *e);
    void UnRegisterEvpnRouteTableListener(const VrfEntry *entry,
                                          VrfState *state);
    void AddIpMacBinding(VrfEntry *vrf, const IpAddress &ip,
                         const MacAddress &mac,
                         const PathPreference::Preference &pref,
                         bool wait_for_traffic);
    void DelIpMacBinding(VrfEntry *vrf, const IpAddress &ip,
                         const MacAddress &mac);
    MacAddress GetIpMacBinding(VrfEntry *vrf, const IpAddress &ip) const;
    bool GetIpMacWaitForTraffic(VrfEntry *vrf,
                                const IpAddress &ip) const;
    void NotifyUcRoute(VrfEntry *vrf, VrfState *state, const IpAddress &ip);
    bool RouteNeedsMacBinding(const InetUnicastRouteEntry *rt);
    DBTableBase::ListenerId vrf_listener_id() const {return vrf_listener_id_;}

private:
    KSync *ksync_;
    DBTableBase::ListenerId vrf_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VrfKSyncObject);
};

class KSyncRouteWalker : public AgentRouteWalker {
public:
    typedef DBTableWalker::WalkId RouteWalkerIdList[Agent::ROUTE_TABLE_MAX];
    KSyncRouteWalker(Agent *agent, VrfKSyncObject::VrfState *state);
    virtual ~KSyncRouteWalker();

    void NotifyRoutes(VrfEntry *vrf);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

private:
    VrfKSyncObject::VrfState *state_;
    DISALLOW_COPY_AND_ASSIGN(KSyncRouteWalker);
};

#endif // vnsw_agent_route_ksync_h
