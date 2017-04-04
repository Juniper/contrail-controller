/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_RIBOUT_H_
#define SRC_BGP_BGP_RIBOUT_H_

#include <boost/scoped_ptr.hpp>
#include <boost/intrusive/slist.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "base/bitset.h"
#include "base/index_map.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_rib_policy.h"
#include "db/db_entry.h"
#include "net/tunnel_encap_type.h"

class IPeer;
class IPeerUpdate;
class RibOutUpdates;
class ShowRibOutStatistics;
class BgpTable;
class BgpExport;
class BgpRoute;
class BgpUpdateSender;
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
    // This nested class represents an ecmp element for a ribout entry. A
    // ribout entry keeps a vector of these elements. Each element stores
    // all per nexthop properties in addition to the nexthop address and
    // label.
    //
    // The origin_vn_index keeps track of the index of the VN from which
    // the BgpPath originated.  A value of -1 means that the VN is unknown.
    class NextHop {
        public:
            NextHop(const BgpTable *table, IpAddress address,
                const MacAddress &mac, uint32_t label, uint32_t l3_label,
                const ExtCommunity *ext_community, bool vrf_originated);

            const IpAddress address() const { return address_; }
            const MacAddress &mac() const { return mac_; }
            uint32_t label() const { return label_; }
            uint32_t l3_label() const { return l3_label_; }
            int origin_vn_index() const { return origin_vn_index_; }
            std::vector<std::string> encap() const { return encap_; }

            int CompareTo(const NextHop &rhs) const;
            bool operator==(const NextHop &rhs) const;
            bool operator!=(const NextHop &rhs) const;

        private:
            IpAddress address_;
            MacAddress mac_;
            uint32_t label_;
            uint32_t l3_label_;
            int origin_vn_index_;
            std::vector<std::string> encap_;
    };

    typedef std::vector<NextHop> NextHopList;

    RibOutAttr(const RibOutAttr &rhs);
    RibOutAttr() : attr_out_(NULL), is_xmpp_(false), vrf_originated_(false) { }
    RibOutAttr(const BgpRoute *route, const BgpAttr *attr, bool is_xmpp);
    RibOutAttr(const BgpTable *table, const BgpAttr *attr, uint32_t label,
        uint32_t l3_label = 0);
    RibOutAttr(const BgpTable *table, const BgpRoute *route,
        const BgpAttr *attr, uint32_t label, bool include_nh = true,
        bool is_xmpp = false);

    RibOutAttr &operator=(const RibOutAttr &rhs);
    bool operator==(const RibOutAttr &rhs) const { return CompareTo(rhs) == 0; }
    bool operator!=(const RibOutAttr &rhs) const { return CompareTo(rhs) != 0; }
    bool IsReachable() const { return attr_out_.get() != NULL; }

    const NextHopList &nexthop_list() const { return nexthop_list_; }
    const BgpAttr *attr() const { return attr_out_.get(); }
    void set_attr(const BgpTable *table, const BgpAttrPtr &attrp) {
        set_attr(table, attrp, 0, 0, false);
    }
    void set_attr(const BgpTable *table, const BgpAttrPtr &attrp,
        uint32_t label) {
        set_attr(table, attrp, label, 0, false);
    }
    void set_attr(const BgpTable *table, const BgpAttrPtr &attrp,
        uint32_t label, uint32_t l3_label, bool vrf_originated);

    void clear() {
        attr_out_.reset();
        nexthop_list_.clear();
    }
    uint32_t label() const {
        return nexthop_list_.empty() ? 0 : nexthop_list_.at(0).label();
    }
    uint32_t l3_label() const {
        return nexthop_list_.empty() ? 0 : nexthop_list_.at(0).l3_label();
    }
    bool is_xmpp() const { return is_xmpp_; }
    bool vrf_originated() const { return vrf_originated_; }
    const std::string &repr() const { return repr_; }
    void set_repr(const std::string &repr, size_t pos = 0) const {
        repr_.clear();
        repr_.append(repr, pos, std::string::npos);
    }

private:
    int CompareTo(const RibOutAttr &rhs) const;

    BgpAttrPtr attr_out_;
    NextHopList nexthop_list_;
    bool is_xmpp_;
    bool vrf_originated_;
    mutable std::string repr_;
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
    explicit AdvertiseInfo(const RibOutAttr *roattr) : roattr(*roattr) { }
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
// This class represents per-table state for a collection of peers with the
// same export policy.  It is effectively a combination of RibExportPolicy
// and BgpTable. A RibOut has a 1:N association with RibOutUpdates wherein
// one entry is created per DB partition.
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

    RibOut(BgpTable *table, BgpUpdateSender *sender,
           const RibExportPolicy &policy);
    ~RibOut();

    void RegisterListener();
    void Register(IPeerUpdate *peer);
    void Unregister(IPeerUpdate *peer);
    bool IsRegistered(IPeerUpdate *peer);
    void Deactivate(IPeerUpdate *peer);
    bool IsActive(IPeerUpdate *peer) const;
    void BuildSendReadyBitSet(const RibPeerSet &peerset,
        RibPeerSet *mready) const;

    IPeerUpdate *GetPeer(int index) const;
    int GetPeerIndex(IPeerUpdate *peer) const;

    // Returns a bitmask with all the peers that are advertising this RibOut.
    const RibPeerSet &PeerSet() const;
    void GetSubsetPeerSet(RibPeerSet *peerset, const IPeerUpdate *cpeer) const;

    BgpTable *table() { return table_; }
    const BgpTable *table() const { return table_; }
    BgpUpdateSender *sender() { return sender_; }

    const RibExportPolicy &ExportPolicy() const { return policy_; }

    int RouteAdvertiseCount(const BgpRoute *rt) const;
    uint32_t GetQueueSize() const;

    DBTableBase::ListenerId listener_id() const { return listener_id_; }
    const std::string &ToString() const { return name_; }

    RibOutUpdates *updates(int idx) { return updates_[idx]; }
    const RibOutUpdates *updates(int idx) const { return updates_[idx]; }
    BgpExport *bgp_export() { return bgp_export_.get(); }

    BgpProto::BgpPeerType peer_type() const { return policy_.type; }
    as_t peer_as() const { return policy_.as_number; }
    bool as_override() const { return policy_.as_override; }
    bool llgr() const { return policy_.llgr; }
    const IpAddress &nexthop() const { return policy_.nexthop; }
    bool IsEncodingXmpp() const {
        return (policy_.encoding == RibExportPolicy::XMPP);
    }
    bool IsEncodingBgp() const {
        return (policy_.encoding == RibExportPolicy::BGP);
    }
    std::string EncodingString() const {
        return IsEncodingXmpp() ? "XMPP" : "BGP";
    }
    bool remove_private_enabled() const {
        return policy_.remove_private.enabled;
    }
    bool remove_private_all() const { return policy_.remove_private.all; }
    bool remove_private_replace() const {
        return policy_.remove_private.replace;
    }
    bool remove_private_peer_loop_check() const {
        return policy_.remove_private.peer_loop_check;
    }

    void FillStatisticsInfo(std::vector<ShowRibOutStatistics> *sros_list) const;

private:
    struct PeerState {
        explicit PeerState(IPeerUpdate *key) : peer(key), index(-1) {
        }
        void set_index(int idx) { index = idx; }
        IPeerUpdate *peer;
        int index;
    };
    typedef IndexMap<IPeerUpdate *, PeerState, RibPeerSet> PeerStateMap;

    BgpTable *table_;
    BgpUpdateSender *sender_;
    RibExportPolicy policy_;
    std::string name_;
    PeerStateMap state_map_;
    RibPeerSet active_peerset_;
    int listener_id_;
    std::vector<RibOutUpdates *> updates_;
    boost::scoped_ptr<BgpExport> bgp_export_;

    DISALLOW_COPY_AND_ASSIGN(RibOut);
};

#endif  // SRC_BGP_BGP_RIBOUT_H_
