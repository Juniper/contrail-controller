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
#include <ksync/ksync_netlink.h>
#include <vrouter/ksync/interface_ksync.h>
#include "oper/nexthop.h"

#include "vr_nexthop.h"

class NHKSyncObject;

class NHKSyncEntry : public KSyncNetlinkDBEntry {
public:
    static const int kDefaultNhMsgSize = 4096;
    NHKSyncEntry(NHKSyncObject *obj, const NHKSyncEntry *entry, 
                 uint32_t index);
    NHKSyncEntry(NHKSyncObject *obj, const NextHop *nh);
    virtual ~NHKSyncEntry();

    const NextHop *nh() { return nh_; }
    NextHop::Type type() const {return type_;}
    InterfaceKSyncEntry *interface() const { 
        return static_cast<InterfaceKSyncEntry *>(interface_.get());
    }
    KSyncDBObject *GetObject() const;

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    void FillObjectLog(sandesh_op::type op, KSyncNhInfo &info) const;
    uint32_t nh_id() const { return nh_id_;}
    void SetEncap(InterfaceKSyncEntry *if_ksync, std::vector<int8_t> &encap);
    bool is_bridge() const { return is_bridge_; }

    int MsgLen() {
        // for larger component NH lists, increase message length
        return kDefaultNhMsgSize * ((component_nh_list_.size() / 128) + 1);
    }
    uint8_t SetEcmpFieldsToUse();
private:
    class KSyncComponentNH {
    public:
        KSyncComponentNH(uint32_t label, KSyncEntry *entry) :
            label_(label), nh_(entry) {
        }

        NHKSyncEntry *nh() const {
            return static_cast<NHKSyncEntry *>(nh_.get());
        }

        uint32_t label() const {
            return label_;
        }
    private:
        uint32_t label_;
        KSyncEntryPtr nh_;
    };

    typedef std::vector<KSyncComponentNH> KSyncComponentNHList;

    int Encode(sandesh_op::type op, char *buf, int buf_len);
    NHKSyncObject *ksync_obj_;
    NextHop::Type type_;
    uint32_t vrf_id_;
    uint32_t label_;
    KSyncEntryPtr interface_;
    IpAddress sip_;
    IpAddress dip_;
    uint16_t sport_;
    uint16_t dport_;
    MacAddress smac_;
    MacAddress dmac_;
    bool valid_;
    bool policy_;
    bool relaxed_policy_;
    bool is_mcast_nh_;
    bool defer_;
    KSyncComponentNHList component_nh_list_;
    const NextHop *nh_;
    uint16_t vlan_tag_;
    bool is_local_ecmp_nh_;
    bool is_bridge_;
    COMPOSITETYPE comp_type_;
    TunnelType tunnel_type_;
    uint8_t prefix_len_;
    uint32_t nh_id_;
    ComponentNHKeyList component_nh_key_list_;
    bool vxlan_nh_;
    bool flood_unknown_unicast_;
    EcmpHashFields ecmp_hash_fieds_;
    KSyncEntryPtr pbb_child_nh_;
    uint32_t isid_;
    uint32_t pbb_label_;
    bool learning_enabled_;
    bool need_pbb_tunnel_;
    bool etree_leaf_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(NHKSyncEntry);
};

class NHKSyncObject : public KSyncDBObject {
public:
    static const int kNHIndexCount = NH_TABLE_ENTRIES;
    NHKSyncObject(KSync *ksync);
    virtual ~NHKSyncObject();

    KSync *ksync() const { return ksync_; }

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();
private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(NHKSyncObject);
};

#endif // vnsw_agent_nh_ksync_h
