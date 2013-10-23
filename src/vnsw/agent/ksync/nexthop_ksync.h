/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_nh_ksync_h
#define vnsw_agent_nh_ksync_h

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "oper/nexthop.h"

#include "vr_nexthop.h"

class NHKSyncObject;

class NHKSyncEntry : public KSyncNetlinkDBEntry {
public:
    NHKSyncEntry(const NHKSyncEntry *entry, uint32_t index) : 
        KSyncNetlinkDBEntry(index), type_(entry->type_),vrf_id_(entry->vrf_id_),
        label_(entry->label_), interface_(entry->interface_),
        sip_(entry->sip_), dip_(entry->dip_), sport_(entry->sport_), 
        dport_(entry->dport_), smac_(entry->smac_), dmac_(entry->dmac_), 
        valid_(entry->valid_), policy_(entry->policy_) , 
        is_mcast_nh_(entry->is_mcast_nh_), defer_(entry->defer_), 
        component_nh_list_(entry->component_nh_list_),
        nh_(entry->nh_), vlan_tag_(entry->vlan_tag_), 
        is_local_ecmp_nh_(entry->is_local_ecmp_nh_),
        is_layer2_(entry->is_layer2_),
        comp_type_(entry->comp_type_), tunnel_type_(entry->tunnel_type_) {
    };

    NHKSyncEntry(const NextHop *nh);
    virtual ~NHKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    KSyncDBObject *GetObject();
    const NextHop *GetNH() { return nh_; };

    NextHop::Type GetType() const {return type_;};
    TunnelType::Type GetTunnelType() const {return tunnel_type_.GetType();};
    IntfKSyncEntry *GetIntf() const { 
        return static_cast<IntfKSyncEntry *>(interface_.get());
    }
    void FillObjectLog(sandesh_op::type op, KSyncNhInfo &info);
private:
    class KSyncComponentNH {
    public:
        KSyncComponentNH(uint32_t label, KSyncEntry *entry) :
            label_(label), nh_(entry) {
        }

        KSyncEntry *GetNH() {
            return nh_.get();
        }

        uint32_t GetLabel() {
            return label_;
        }
    private:
        uint32_t label_;
        KSyncEntryPtr nh_;
    };

    typedef std::vector<KSyncComponentNH> KSyncComponentNHList;

    int Encode(sandesh_op::type op, char *buf, int buf_len);
    NextHop::Type type_;
    uint32_t vrf_id_;
    uint32_t label_;
    KSyncEntryPtr interface_;
    struct in_addr sip_;
    struct in_addr dip_;
    uint16_t sport_;
    uint16_t dport_;
    struct ether_addr smac_;
    struct ether_addr dmac_;
    bool valid_;
    bool policy_;
    bool is_mcast_nh_;
    bool defer_;
    KSyncComponentNHList component_nh_list_;
    const NextHop *nh_;
    uint16_t vlan_tag_;
    bool is_local_ecmp_nh_;
    bool is_layer2_;
    COMPOSITETYPE comp_type_;
    TunnelType tunnel_type_;
    DISALLOW_COPY_AND_ASSIGN(NHKSyncEntry);
};

class NHKSyncObject : public KSyncDBObject {
public:
    static const int kNHIndexCount = NH_TABLE_ENTRIES;
    NHKSyncObject(DBTableBase *table) : 
        KSyncDBObject(table, kNHIndexCount) {};

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const NHKSyncEntry *nh = static_cast<const NHKSyncEntry *>(entry);
        NHKSyncEntry *ksync = new NHKSyncEntry(nh, index);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const NextHop *nh = static_cast<const NextHop *>(e);
        NHKSyncEntry *key = new NHKSyncEntry(nh);
        return static_cast<KSyncEntry *>(key);
    }

    static void Init(NextHopTable *table) {
        assert(singleton_ == NULL);
        singleton_ = new NHKSyncObject(table);
    };

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    static NHKSyncObject *GetKSyncObject() { return singleton_; };

private:
    static NHKSyncObject *singleton_;
    DISALLOW_COPY_AND_ASSIGN(NHKSyncObject);

};

#endif // vnsw_agent_nh_ksync_h
