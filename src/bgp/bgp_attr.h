/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_attr_h
#define ctrlplane_bgp_attr_h

#include <set>
#include <vector>
#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

#include "base/label_block.h"
#include "base/parse_object.h"
#include "base/util.h"

#include "bgp/bgp_aspath.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_server.h"
#include "bgp/community.h"

#include "net/address.h"
#include "net/rd.h"

// BGP UPDATE attributes: as-path, community, ext-community, next-hop,
// cluster-list, ...
// all information in the UPDATE except: NLRI (prefix) and label.

class BgpAttr;
class AsPathDB;

struct BgpAttrOrigin : public BgpAttribute {
    static const int kSize = 1;
    static const uint8_t kFlags = Transitive;
    BgpAttrOrigin() : BgpAttribute(Origin, kFlags), origin(IGP) { }
    BgpAttrOrigin(const BgpAttribute &rhs) : BgpAttribute(rhs), origin(IGP) { }
    explicit BgpAttrOrigin(int origin) :
            BgpAttribute(Origin, kFlags), origin(origin) { }
    enum OriginType {
        IGP = 0,
        EGP = 1,
        INCOMPLETE = 2
    };

    int origin;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrNextHop : public BgpAttribute {
    static const int kSize = 4;
    static const uint8_t kFlags = Transitive;
    BgpAttrNextHop() : BgpAttribute(NextHop, kFlags), nexthop(0) {}
    BgpAttrNextHop(const BgpAttribute &rhs) : BgpAttribute(rhs), nexthop(0) {}
    explicit BgpAttrNextHop(uint32_t nexthop) :
            BgpAttribute(NextHop, kFlags), nexthop(nexthop) {}
    uint32_t nexthop;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrMultiExitDisc : public BgpAttribute {
    static const int kSize = 4;
    static const uint8_t kFlags = Optional;
    BgpAttrMultiExitDisc() : BgpAttribute(MultiExitDisc, kFlags), med(0) {}
    BgpAttrMultiExitDisc(const BgpAttribute &rhs) : BgpAttribute(rhs), med(0) {}
    explicit BgpAttrMultiExitDisc(uint32_t med) :
            BgpAttribute(MultiExitDisc, kFlags), med(med) {}
    uint32_t med;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrLocalPref : public BgpAttribute {
    static const int kDefault = 100;
    static const int kSize = 4;
    static const uint8_t kFlags = Transitive;
    BgpAttrLocalPref() : BgpAttribute(LocalPref, kFlags), local_pref(0) {}
    BgpAttrLocalPref(const BgpAttribute &rhs) :
        BgpAttribute(rhs), local_pref(0) {}
    explicit BgpAttrLocalPref(uint32_t local_pref) :
            BgpAttribute(LocalPref, kFlags), local_pref(local_pref) {}
    uint32_t local_pref;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrAtomicAggregate : public BgpAttribute {
    static const int kSize = 0;
    static const uint8_t kFlags = Transitive;
    BgpAttrAtomicAggregate() : BgpAttribute(AtomicAggregate, kFlags) {}
    BgpAttrAtomicAggregate(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrAggregator : public BgpAttribute {
    static const int kSize = 6;
    static const uint8_t kFlags = Optional|Transitive;
    BgpAttrAggregator() :
        BgpAttribute(Aggregator, kFlags), as_num(0), address(0) {}
    BgpAttrAggregator(const BgpAttribute &rhs) :
        BgpAttribute(rhs), as_num(0), address(0) {}
    explicit BgpAttrAggregator(uint32_t as_num, uint32_t address) :
        BgpAttribute(Aggregator, kFlags), as_num(as_num), address(address) {}
    as_t as_num;
    uint32_t address;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpMpNlri : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Optional;
    // Always using extended length for MP NLRI
    BgpMpNlri() : BgpAttribute(0, ExtendedLength|kFlags), afi(0), safi(0) {}
    explicit BgpMpNlri(BgpAttribute::Code code) :
            BgpAttribute(code, ExtendedLength|kFlags), afi(0), safi(0) {}
    explicit BgpMpNlri(BgpAttribute::Code code, u_int16_t afi, u_int8_t safi, 
                       std::vector<uint8_t> nh) : 
            BgpAttribute(code, ExtendedLength|kFlags), afi(afi), safi(safi), 
                       nexthop(nh) {}
    explicit BgpMpNlri(BgpAttribute::Code code, u_int16_t afi, u_int8_t safi) :
        BgpAttribute(code, ExtendedLength|kFlags), afi(afi), safi(safi) {
        nexthop.clear();
    }
    explicit BgpMpNlri(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    ~BgpMpNlri() {
        STLDeleteValues(&nlri);
    }
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);

    uint16_t afi;
    uint8_t safi;
    std::vector<uint8_t> nexthop;

    std::vector<BgpProtoPrefix *> nlri;
};

struct EdgeDiscoverySpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Optional | Transitive;
    EdgeDiscoverySpec();
    explicit EdgeDiscoverySpec(const BgpAttribute &rhs);
    explicit EdgeDiscoverySpec(const EdgeDiscoverySpec &rhs);
    ~EdgeDiscoverySpec();

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;

    struct Edge : public ParseObject {
        Ip4Address GetIp4Address() const;
        void SetIp4Address(Ip4Address addr);
        void GetLabels(uint32_t *first_label, uint32_t *last_label) const;
        void SetLabels(uint32_t first_label, uint32_t last_label);

        std::vector<uint8_t> address;
        std::vector<uint32_t> labels;
    };
    typedef std::vector<Edge *> EdgeList;
    EdgeList edge_list;
};

class EdgeDiscovery {
public:
    explicit EdgeDiscovery(const EdgeDiscoverySpec &edspec);
    ~EdgeDiscovery();
    const EdgeDiscoverySpec &edge_discovery() const { return edspec_; }

    struct Edge {
        Edge(const EdgeDiscoverySpec::Edge *edge_spec);
        Ip4Address address;
        LabelBlockPtr label_block;
    };
    typedef std::vector<Edge *> EdgeList;

    EdgeList edge_list;

private:
    friend void intrusive_ptr_add_ref(EdgeDiscovery *ediscovery);
    friend void intrusive_ptr_release(EdgeDiscovery *ediscovery);

    tbb::atomic<int> refcount_;
    EdgeDiscoverySpec edspec_;
};

inline void intrusive_ptr_add_ref(EdgeDiscovery *ediscovery) {
    ediscovery->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(EdgeDiscovery *ediscovery) {
    int prev = ediscovery->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete ediscovery;
    }
}

typedef boost::intrusive_ptr<EdgeDiscovery> EdgeDiscoveryPtr;

struct EdgeForwardingSpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Optional | Transitive;
    EdgeForwardingSpec();
    explicit EdgeForwardingSpec(const BgpAttribute &rhs);
    explicit EdgeForwardingSpec(const EdgeForwardingSpec &rhs);
    ~EdgeForwardingSpec();

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;

    struct Edge : public ParseObject {
        Ip4Address GetInboundIp4Address() const;
        Ip4Address GetOutboundIp4Address() const;
        void SetInboundIp4Address(Ip4Address addr);
        void SetOutboundIp4Address(Ip4Address addr);

        int address_len;
        std::vector<uint8_t> inbound_address, outbound_address;
        uint32_t inbound_label, outbound_label;
    };
    typedef std::vector<Edge *> EdgeList;

    EdgeList edge_list;
};

class EdgeForwarding {
public:
    explicit EdgeForwarding(const EdgeForwardingSpec &efspec);
    ~EdgeForwarding();
    const EdgeForwardingSpec &edge_forwarding() const { return efspec_; }

    struct Edge {
        Edge(const EdgeForwardingSpec::Edge *edge_spec);
        Ip4Address inbound_address, outbound_address;
        uint32_t inbound_label, outbound_label;
    };
    typedef std::vector<Edge *> EdgeList;

    EdgeList edge_list;

private:
    friend void intrusive_ptr_add_ref(EdgeForwarding *eforwarding);
    friend void intrusive_ptr_release(EdgeForwarding *eforwarding);

    tbb::atomic<int> refcount_;
    EdgeForwardingSpec efspec_;
};

inline void intrusive_ptr_add_ref(EdgeForwarding *eforwarding) {
    eforwarding->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(EdgeForwarding *eforwarding) {
    int prev = eforwarding->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete eforwarding;
    }
}

typedef boost::intrusive_ptr<EdgeForwarding> EdgeForwardingPtr;

struct BgpAttrLabelBlock : public BgpAttribute {
    static const int kSize = 0;
    BgpAttrLabelBlock() : BgpAttribute(0, BgpAttribute::LabelBlock, 0) {}
    BgpAttrLabelBlock(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    BgpAttrLabelBlock(LabelBlockPtr label_block) :
        BgpAttribute(0, BgpAttribute::LabelBlock, 0),
        label_block(label_block) {}
    LabelBlockPtr label_block;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

class BgpOListElem {
public:
    BgpOListElem(Ip4Address address, uint32_t label,
        std::vector<std::string> encap = std::vector<std::string>())
        : address(address), label(label), encap(encap) {
    }

    friend std::size_t hash_value(BgpOListElem const &elem) {
        size_t hash = 0;
        boost::hash_combine(hash, elem.address.to_string());
        boost::hash_combine(hash, elem.label);
        return hash;
    }

    Ip4Address address;
    uint32_t label;
    std::vector<std::string> encap;
};

class BgpOList {
public:
    BgpOList() { refcount_ = 0; }

    std::vector<BgpOListElem> elements;

private:
    friend void intrusive_ptr_add_ref(BgpOList *olist);
    friend void intrusive_ptr_release(BgpOList *olist);

    tbb::atomic<int> refcount_;
};

inline void intrusive_ptr_add_ref(BgpOList *olist) {
    olist->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(BgpOList *olist) {
    int prev = olist->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete olist;
    }
}

typedef boost::intrusive_ptr<BgpOList> BgpOListPtr;

struct BgpAttrOList : public BgpAttribute {
    static const int kSize = 0;
    BgpAttrOList() : BgpAttribute(0, BgpAttribute::OList, 0) {}
    BgpAttrOList(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    BgpAttrOList(BgpOList *olist) :
        BgpAttribute(0, BgpAttribute::OList, 0), olist(olist) {}
    BgpAttrOList(BgpOListPtr olist) :
        BgpAttribute(0, BgpAttribute::OList, 0), olist(olist) {}
    BgpOListPtr olist;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrUnknown : public BgpAttribute {
    BgpAttrUnknown() : BgpAttribute() {}
    explicit BgpAttrUnknown(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    std::vector<uint8_t> value;
};

struct BgpAttrSourceRd : public BgpAttribute {
    BgpAttrSourceRd() : BgpAttribute(0, SourceRd, 0) {}
    BgpAttrSourceRd(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    explicit BgpAttrSourceRd(RouteDistinguisher source_rd) :
            BgpAttribute(0, SourceRd, 0), source_rd(source_rd) {}
    RouteDistinguisher source_rd;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

typedef std::vector<BgpAttribute *> BgpAttrSpec;

// Canonicalized BGP attribute
class BgpAttr {
public:
    BgpAttr();
    BgpAttr(BgpAttrDB *attr_db);
    BgpAttr(const BgpAttr &rhs);
    BgpAttr(BgpAttrDB *attr_db, const BgpAttrSpec &spec);
    virtual ~BgpAttr() { }

    virtual void Remove();
    int CompareTo(const BgpAttr &rhs) const;

    void set_origin(BgpAttrOrigin::OriginType org) { origin_ = org; }
    void set_nexthop(IpAddress nexthop) { nexthop_ = nexthop; }
    void set_med(uint32_t med) { med_ = med; }
    void set_local_pref(uint32_t local_pref) { local_pref_ = local_pref; }
    void set_atomic_aggregate(bool ae) { atomic_aggregate_ = ae; }
    void set_aggregator(as_t as_num, IpAddress address) {
        aggregator_as_num_ = as_num;
        aggregator_address_ = address;
    }
    void set_source_rd(RouteDistinguisher source_rd) { source_rd_ = source_rd; }
    void set_as_path(const AsPathSpec *spec);
    void set_community(const CommunitySpec *comm);
    void set_ext_community(ExtCommunityPtr comm);
    void set_ext_community(const ExtCommunitySpec *extcomm);
    void set_edge_discovery(const EdgeDiscoverySpec *edspec);
    void set_edge_forwarding(const EdgeForwardingSpec *efspec);
    void set_label_block(LabelBlockPtr label_block);
    void set_olist(BgpOListPtr olist);
    friend std::size_t hash_value(BgpAttr const &attr);

    BgpAttrOrigin::OriginType origin() const { return origin_; }
    const IpAddress &nexthop() const { return nexthop_; }
    uint32_t med() const { return med_; }
    uint32_t local_pref() const { return local_pref_; }
    bool atomic_aggregate() const { return atomic_aggregate_; }
    as_t aggregator_as_num() const { return aggregator_as_num_; }
    uint32_t neighbor_as() const;
    const IpAddress &aggregator_adderess() const { return aggregator_address_; }
    RouteDistinguisher source_rd() const { return source_rd_; }
    const AsPath *as_path() const { return as_path_.get(); }
    int as_path_count() const { return as_path_ ? as_path_->AsCount() : 0; }
    const Community *community() const { return community_.get(); }
    const ExtCommunity *ext_community() const { return ext_community_.get(); }
    const EdgeDiscovery *edge_discovery() const { return edge_discovery_.get(); }
    const EdgeForwarding *edge_forwarding() const { return edge_forwarding_.get(); }
    LabelBlockPtr label_block() const { return label_block_; }
    BgpOListPtr olist() const { return olist_; }
    BgpAttrDB *attr_db() const { return attr_db_; }

private:
    friend class BgpAttrDB;
    friend int intrusive_ptr_add_ref(const BgpAttr *cattrp);
    friend int intrusive_ptr_del_ref(const BgpAttr *cattrp);
    friend void intrusive_ptr_release(const BgpAttr *cattrp);

    mutable tbb::atomic<int> refcount_;
    BgpAttrDB *attr_db_;
    BgpAttrOrigin::OriginType origin_;
    IpAddress nexthop_;
    uint32_t med_;
    uint32_t local_pref_;
    bool atomic_aggregate_;
    as_t aggregator_as_num_;
    IpAddress aggregator_address_;
    RouteDistinguisher source_rd_;
    AsPathPtr as_path_;
    CommunityPtr community_;
    ExtCommunityPtr ext_community_;
    EdgeDiscoveryPtr edge_discovery_;
    EdgeForwardingPtr edge_forwarding_;
    LabelBlockPtr label_block_;
    BgpOListPtr olist_;
};

inline int intrusive_ptr_add_ref(const BgpAttr *cattrp) {
    return cattrp->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const BgpAttr *cattrp) {
    return cattrp->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const BgpAttr *cattrp) {
    int prev = cattrp->refcount_.fetch_and_decrement();
    if (prev == 1) {
        BgpAttr *attrp = const_cast<BgpAttr *>(cattrp);
        attrp->Remove();
        assert(attrp->refcount_ == 0);
        delete attrp;
    }
}

typedef boost::intrusive_ptr<const BgpAttr> BgpAttrPtr;

struct BgpAttrCompare {
    bool operator()(const BgpAttr *lhs, const BgpAttr *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class BgpAttrDB : public BgpPathAttributeDB<BgpAttr, BgpAttrPtr, BgpAttrSpec,
                                            BgpAttrCompare, BgpAttrDB> {
public:
    BgpAttrDB(BgpServer *server);
    BgpAttrPtr ReplaceExtCommunityAndLocate(const BgpAttr *attr,
                                            ExtCommunityPtr com);
    BgpAttrPtr ReplaceLocalPreferenceAndLocate(const BgpAttr *attr, 
                                               uint32_t local_pref);
    BgpAttrPtr ReplaceSourceRdAndLocate(const BgpAttr *attr,
                                        RouteDistinguisher source_rd);
    BgpAttrPtr ReplaceMulticastEdgeDiscoveryAndLocate(const BgpAttr *attr,
                                        BgpAttrPtr edge_discovery_attribute) {
        return attr;
    }
    BgpAttrPtr UpdateNexthopAndLocate(const BgpAttr *attr, uint16_t afi, 
                                      uint8_t safi, IpAddress &addr);
    BgpServer *server() { return server_; }

private:
    BgpServer *server_;
};

#endif
