/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_ribout_h
#define ctrlplane_bgp_ribout_h

#include <boost/scoped_ptr.hpp>
#include <boost/intrusive/slist.hpp>

#include "base/bitset.h"
#include "base/index_map.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_proto.h"
#include "db/db_entry.h"
#include "net/tunnel_encap_type.h"

class IPeer;
class IPeerUpdate;
class RibOutUpdates;
class SchedulingGroup;
class SchedulingGroupManager;
class BgpTable;
class BgpExport;
class BgpRoute;
class RouteUpdate;
class UpdateInfoSList;

//
// This class represents the attributes for a ribout entry, including the
// label.  It is essentially a combination of a smart pointer to BgpAttr
// and a label. The label is not included in BgpAttr in order to maximize
// sharing of the BgpAttr.
//
class RibOutAttr {
public:
    class NextHop {
        public:
            NextHop(IpAddress address, uint32_t label, 
                    const ExtCommunity *ext_community)
                : address_(address), label_(label) {
                if (ext_community)
                    encap_ = ext_community->GetTunnelEncap();
            }
            const IpAddress address() const { return address_; }
            uint32_t label() const { return label_; }
            std::vector<std::string> encap() const { return encap_; }

            int CompareTo(const NextHop &rhs) const {
                if (address_ < rhs.address_) return -1;
                if (address_ > rhs.address_) return 1;
                if (label_ < rhs.label_) return -1;
                if (label_ > rhs.label_) return 1;
                if (encap_.size() < rhs.encap_.size()) return -1;
                if (encap_.size() > rhs.encap_.size()) return 1;
                for (size_t idx = 0; idx < encap_.size(); idx++) {
                    if (encap_[idx] < rhs.encap_[idx]) return -1;
                    if (encap_[idx] > rhs.encap_[idx]) return 1;
                }
                return 0;
            }

            bool operator==(const NextHop &rhs) const {
                return CompareTo(rhs) == 0;
            }

            bool operator!=(const NextHop &rhs) const {
                return CompareTo(rhs) != 0;
            }

        private:
            IpAddress address_;
            uint32_t  label_;
            std::vector<std::string> encap_;
    };

    typedef std::vector<NextHop> NextHopList;

    RibOutAttr() : attr_out_(NULL) { }
    RibOutAttr(const BgpAttr *attr, uint32_t label, bool include_nh = true);
    RibOutAttr(BgpRoute *route, const BgpAttr *attr, bool is_xmpp);

    bool IsReachable() const { return attr_out_.get() != NULL; }
    bool operator==(const RibOutAttr &rhs) const { return CompareTo(rhs) == 0; }
    bool operator!=(const RibOutAttr &rhs) const { return CompareTo(rhs) != 0; }

    const NextHopList &nexthop_list() const { return nexthop_list_; }
    const BgpAttr *attr() const { return attr_out_.get(); }
    void set_attr(const BgpAttrPtr &attrp, uint32_t label = 0);

    void clear() {
        attr_out_.reset();
        nexthop_list_.clear();
    }
    uint32_t label() const {
        return nexthop_list_.empty() ? 0 : nexthop_list_.at(0).label();
    }

private:
    int CompareTo(const RibOutAttr &rhs) const;

    BgpAttrPtr attr_out_;
    NextHopList nexthop_list_;
};

//
// This class represents a bitset of peers within a RibOut. This is distinct
// from the GroupPeerSet in order to allow it to be denser. This is possible
// because not every peer in the group may be interested in every RibOut.
//
class RibPeerSet : public BitSet {
};

//
// This class represents information for a particular prefix that has been
// advertised to a set of peers.
//
// An AdvertiseInfo is part of a intrusive singly linked list container in
// the RouteState.  The RibPeerSet represents the set of peers to which the
// prefix has been advertised and the RibOutAttr represents the associated
// attributes.  This representation allows us to keep track of a different
// set of attributes for each set of peers.
//
struct AdvertiseInfo {
    AdvertiseInfo() { }
    AdvertiseInfo(const RibOutAttr *roattr) : roattr(*roattr) { }
    AdvertiseInfo(const AdvertiseInfo &rhs)
        : bitset(rhs.bitset), roattr(rhs.roattr) {
    }

    // Intrusive slist node for RouteState.
    boost::intrusive::slist_member_hook<> slist_node;

    RibPeerSet bitset;
    RibOutAttr roattr;
};

//
// Disposer for AdvertiseInfo.
//
struct AdvertiseInfoDisposer {
    void operator()(AdvertiseInfo *ainfo) { delete ainfo; }
};

//
// Wrapper for intrusive slist of AdvertiseInfos. Destructor automatically
// deletes any elements still on the slist.
//
// TBD: create a class template.
//
class AdvertiseSList {
public:
    typedef boost::intrusive::member_hook<
        AdvertiseInfo,
        boost::intrusive::slist_member_hook<>,
        &AdvertiseInfo::slist_node
    > Node;
    typedef boost::intrusive::slist<
        AdvertiseInfo,
        Node,
        boost::intrusive::linear<true>
    > List;

    AdvertiseSList() { }
    ~AdvertiseSList() { list_.clear_and_dispose(AdvertiseInfoDisposer()); }

    List *operator->() { return &list_; }
    const List *operator->() const { return &list_; }
    const List &list() const { return list_; }
    void swap(AdvertiseSList &adv_slist) { list_.swap(adv_slist.list_); }

private:
    List list_;
};

//
// This class represents per prefix information that been advertised to
// the peers in a RibOut.
//
// A RouteState is derived from a DBState which means that it is part of
// the state map within a DBEntry.  This allows the RouteState to be
// associated with a DBEntry using the listener id of the RibOut as the
// index. In the steady state i.e. when there are no pending updates the
// DBEntry maps the listener id for the RibOut to a RouteState.
//
// A RouteState maintains a singly linked list of AdvertiseInfo entries
// to keep track of the attributes that have been advertised to each set
// of peers.
//
class RouteState : public DBState {
public:
    RouteState();

    void SetHistory(AdvertiseSList &history) {
        assert(advertised_->empty());
        advertised_.swap(history);
    }
    void SwapHistory(AdvertiseSList &history) {
        advertised_.swap(history);
    }
    void MoveHistory(RouteUpdate *rt_update);
    const AdvertiseInfo *FindHistory(const RibOutAttr &roattr) const;
    bool CompareUpdateInfo(const UpdateInfoSList &uinfo_slist) const;

    const AdvertiseSList &Advertised() const { return advertised_; }
    AdvertiseSList &Advertised() { return advertised_; }

private:
    AdvertiseSList advertised_;
    DISALLOW_COPY_AND_ASSIGN(RouteState);
};

//
// This class represents the export policy for a rib. Given that we do not
// currently support any real policy configuration, this is pretty trivial
// for now.
//
// Including the AS number as part of the policy results in creation of a
// different RibOut for every neighbor AS that we peer with. This allows a
// simplified implementation of the sender side AS path loop check. In most
// practical deployment scenarios all eBGP peers will belong to the same
// neighbor AS anyway.
//
// Including the CPU affinity as part of the RibExportPolicy allows us to
// artificially create more RibOuts than otherwise necessary. This is used
// to achieve higher concurrency at the expense of creating more state.
//
struct RibExportPolicy {    
    enum Encoding {
        BGP,
        XMPP,
    };
    
    RibExportPolicy()
        : type(BgpProto::IBGP), encoding(BGP),
          as_number(0), affinity(-1), cluster_id(0) {
    }

    RibExportPolicy(BgpProto::BgpPeerType type, Encoding encoding,
            int affinity, u_int32_t cluster_id)
        : type(type), encoding(encoding), as_number(0),
          affinity(affinity), cluster_id(cluster_id) {
        if (encoding == XMPP)
            assert(type == BgpProto::XMPP);
        if (encoding == BGP)
            assert(type == BgpProto::IBGP || type == BgpProto::EBGP);
    }

    RibExportPolicy(BgpProto::BgpPeerType type, Encoding encoding,
            as_t as_number, int affinity, u_int32_t cluster_id)
        : type(type), encoding(encoding), as_number(as_number),
          affinity(affinity), cluster_id(cluster_id) {
        if (encoding == XMPP)
            assert(type == BgpProto::XMPP);
        if (encoding == BGP)
            assert(type == BgpProto::IBGP || type == BgpProto::EBGP);
    }

    bool operator<(const RibExportPolicy &rhs) const;

    BgpProto::BgpPeerType type;
    Encoding encoding;
    as_t as_number;
    int affinity;
    uint32_t cluster_id;
};

//
// This class represents per-table state for a collection of peers with the
// same export policy.  It is effectively a combination of RibExportPolicy
// and BgpTable. A RibOut has a 1:1 association with RibOutUpdates to which
// it maintains a smart pointer.
//
// A RibOut maintains a PeerStateMap to facilitate allocation and lookup of
// a bit index per peer in the RibOut.
//
class RibOut {
public:
    class PeerIterator {
    public:
        PeerIterator(const RibOut *ribout, const RibPeerSet &peer_set)
            : ribout_(ribout), peer_set_(peer_set) {
            index_ = peer_set_.find_first();
        }
        bool HasNext() const {
            return index_ != RibPeerSet::npos;
        }
        IPeerUpdate *Next() {
            IPeerUpdate *ptr = ribout_->GetPeer(index_);
            index_ = peer_set_.find_next(index_);
            return ptr;
        }
        int index() const { return index_; }

    private:
        const RibOut *ribout_;
        const RibPeerSet &peer_set_;
        size_t index_;
    };
    
    RibOut(BgpTable *table, SchedulingGroupManager *mgr,
           const RibExportPolicy &policy);
    ~RibOut();

    void RegisterListener();
    void Register(IPeerUpdate *peer);
    void Unregister(IPeerUpdate *peer);
    bool IsRegistered(IPeerUpdate *peer);
    void Deactivate(IPeerUpdate *peer);
    bool IsActive(IPeerUpdate *peer) const;

    SchedulingGroup *GetSchedulingGroup();

    IPeerUpdate *GetPeer(int index) const;
    int GetPeerIndex(IPeerUpdate *peer) const;

    // Returns a bitmask with all the peers that are advertising this RibOut.
    const RibPeerSet &PeerSet() const;

    BgpTable* table() const { return table_; }
    
    const RibExportPolicy &ExportPolicy() const { return policy_; }

    int RouteAdvertiseCount(const BgpRoute *rt) const;

    DBTableBase::ListenerId listener_id() const { return listener_id_; }

    RibOutUpdates *updates() { return updates_.get(); }
    BgpExport *bgp_export() { return bgp_export_.get(); }

    BgpProto::BgpPeerType peer_type() const { return policy_.type; }
    as_t peer_as() const { return policy_.as_number; }
    bool IsEncodingXmpp() const {
        return (policy_.encoding == RibExportPolicy::XMPP);
    }
    bool IsEncodingBgp() const {
        return (policy_.encoding == RibExportPolicy::BGP);
    }

private:
    struct PeerState {
        PeerState(IPeerUpdate *key) : peer(key), index(-1) {
        }
        void set_index(int idx) { index = idx; }
        IPeerUpdate *peer;
        int index;
    };
    typedef IndexMap<IPeerUpdate *, PeerState, RibPeerSet> PeerStateMap;
    BgpTable *table_;
    SchedulingGroupManager *mgr_;
    RibExportPolicy policy_;
    PeerStateMap state_map_;
    RibPeerSet active_peerset_;
    int listener_id_;
    boost::scoped_ptr<RibOutUpdates> updates_;
    boost::scoped_ptr<BgpExport> bgp_export_;
    
    DISALLOW_COPY_AND_ASSIGN(RibOut);
};

#endif
