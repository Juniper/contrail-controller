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
#include "ksync/agent_ksync_types.h"
#include "ksync/nexthop_ksync.h"

#define RT_LAYER2 2

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
    NHKSyncEntry* nh() const { 
        return static_cast<NHKSyncEntry *>(nh_.get());
    }
    void set_prefix_len(uint32_t len) { prefix_len_ = len; }
    void set_ip(IpAddress addr) { addr_ = addr; }
    KSyncDBObject *GetObject();

    void FillObjectLog(sandesh_op::type op, KSyncRouteInfo &info) const;
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
private:
    int Encode(sandesh_op::type op, uint8_t replace_plen,
               char *buf, int buf_len);
    int DeleteInternal(NHKSyncEntry *nh, uint32_t lbl, uint8_t replace_plen,
                       bool proxy_arp, char *buf, int buf_len);
    bool UcIsLess(const KSyncEntry &rhs) const;
    bool McIsLess(const KSyncEntry &rhs) const;
    bool L2IsLess(const KSyncEntry &rhs) const;
    RouteKSyncObject* ksync_obj_;
    uint32_t rt_type_;
    uint32_t vrf_id_;
    IpAddress addr_;
    IpAddress src_addr_;
    MacAddress mac_;
    uint32_t prefix_len_;
    KSyncEntryPtr nh_;
    uint32_t label_;
    uint8_t type_;
    bool proxy_arp_;
    string address_string_;
    TunnelType::Type tunnel_type_;
    bool wait_for_traffic_;
    DISALLOW_COPY_AND_ASSIGN(RouteKSyncEntry);
};

class RouteKSyncObject : public KSyncDBObject {
public:
    typedef std::map<uint32_t, RouteKSyncObject *> VrfRtObjectMap;
    struct VrfState : DBState {
        VrfState() : DBState(), seen_(false) {};
        bool seen_;
    };

    RouteKSyncObject(KSync *ksync, AgentRouteTable *rt_table);
    virtual ~RouteKSyncObject();

    KSync *ksync() const { return ksync_; }

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void ManagedDelete();
    void Unregister();
    virtual void EmptyTable();

private:
    KSync *ksync_;
    bool marked_delete_;
    AgentRouteTable *rt_table_;
    LifetimeRef<RouteKSyncObject> table_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(RouteKSyncObject);
};

class VrfKSyncObject {
public:
    struct VrfState : DBState {
        VrfState() : DBState(), seen_(false) {};
        bool seen_;
    };

    VrfKSyncObject(KSync *ksync);
    virtual ~VrfKSyncObject();

    KSync *ksync() const { return ksync_; }

    void RegisterDBClients();
    void Shutdown();
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void AddToVrfMap(uint32_t vrf_id, RouteKSyncObject *,
                     unsigned int table_id);
    void DelFromVrfMap(RouteKSyncObject *);
private:
    KSync *ksync_;
    DBTableBase::ListenerId vrf_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VrfKSyncObject);
};

#endif // vnsw_agent_route_ksync_h
