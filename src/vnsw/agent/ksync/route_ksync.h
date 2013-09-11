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
#include <ksync/mpls_ksync.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "oper/nexthop.h"
#include "oper/inet4_ucroute.h"
#include "oper/inet4_mcroute.h"
#include "ksync/agent_ksync_types.h"

#define RT_UCAST 0
#define RT_MCAST 1

class RouteKSyncEntry : public KSyncNetlinkDBEntry {
public:
    RouteKSyncEntry(const RouteKSyncEntry *entry, uint32_t index) : 
        KSyncNetlinkDBEntry(index), rt_type_(entry->rt_type_), 
        vrf_id_(entry->vrf_id_), addr_(entry->addr_),
        src_addr_(entry->src_addr_), plen_(entry->plen_), nh_(entry->nh_),
        label_(entry->label_), proxy_arp_(false) {
    };

    RouteKSyncEntry(const Inet4Route *route);
    virtual ~RouteKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    KSyncDBObject *GetObject();
    void SetPLen(uint32_t len) {
        plen_ = len;
    }
    void SetIp(IpAddress addr) { addr_ = addr; }
    uint32_t GetPLen() const { return plen_; }
    uint32_t GetLabel() const { return label_; }
    bool GetProxyArp() const { return proxy_arp_; }
    NHKSyncEntry* GetNH() const { 
        return static_cast<NHKSyncEntry *>(nh_.get());
    }
    void FillObjectLog(sandesh_op::type op, KSyncRouteInfo &info);
private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    int DeleteInternal(NHKSyncEntry *nh, uint32_t lbl, bool proxy_arp,
                       char *buf, int buf_len);
    bool UcIsLess(const KSyncEntry &rhs) const;
    bool McIsLess(const KSyncEntry &rhs) const;
    uint32_t rt_type_;
    uint32_t vrf_id_;
    IpAddress addr_;
    IpAddress src_addr_;
    uint32_t plen_;
    KSyncEntryPtr nh_;
    uint32_t label_;
    uint8_t type_;
    bool proxy_arp_;
    DISALLOW_COPY_AND_ASSIGN(RouteKSyncEntry);
};

class RouteKSyncObject : public KSyncDBObject {
public:
    typedef std::map<uint32_t, RouteKSyncObject *> VrfRtObjectMap;
    struct VrfState : DBState {
        VrfState() : DBState(), seen_(false) {};
        bool seen_;
    };

    RouteKSyncObject(Inet4RouteTable *rt_table);
    virtual ~RouteKSyncObject();
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const RouteKSyncEntry *route = static_cast<const RouteKSyncEntry *>(entry);
        RouteKSyncEntry *ksync = new RouteKSyncEntry(route, index);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const Inet4Route *route = static_cast<const Inet4Route *>(e);
        RouteKSyncEntry *key = new RouteKSyncEntry(route);
        return static_cast<KSyncEntry *>(key);
    };

    void ManagedDelete();
    bool IsDeleteMarked() {return marked_delete_;};
    void Unregister();
    virtual void EmptyTable();

private:
    bool marked_delete_;
    Inet4RouteTable *rt_table_;
    LifetimeRef<RouteKSyncObject> table_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(RouteKSyncObject);
};

class VrfKSyncObject {
public:
    typedef std::map<uint32_t, RouteKSyncObject *> VrfRtObjectMap;
    struct VrfState : DBState {
        VrfState() : DBState(), seen_(false) {};
        bool seen_;
    };

    VrfKSyncObject() {};
    virtual ~VrfKSyncObject() {};

    static void Init(VrfTable *vrf_table);
    static void Shutdown();
    static void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    static VrfKSyncObject *GetKSyncObject() {return singleton_;};

    void AddToVrfMap(uint32_t vrf_id, RouteKSyncObject *,
                     unsigned int table_id);
    void DelFromVrfMap(RouteKSyncObject *);
    RouteKSyncObject *GetRouteKSyncObject(uint32_t vrf_id,
                                          unsigned int table_id);

private:
    static VrfKSyncObject *singleton_;
    DBTableBase::ListenerId vrf_listener_id_;
    VrfRtObjectMap vrf_ucrt_object_map_;
    VrfRtObjectMap vrf_mcrt_object_map_;
    DISALLOW_COPY_AND_ASSIGN(VrfKSyncObject);
};

#endif // vnsw_agent_route_ksync_h
