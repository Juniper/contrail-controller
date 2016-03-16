/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_ATTR_H_
#define SRC_BGP_BGP_ATTR_H_

#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

#include <set>
#include <string>
#include <vector>

#include "base/label_block.h"
#include "base/parse_object.h"
#include "base/util.h"
#include "bgp/bgp_aspath.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_origin_vn_path.h"
#include "bgp/community.h"
#include "net/address.h"
#include "net/esi.h"
#include "net/rd.h"

class BgpAttr;
class BgpAttrDB;
class BgpOListDB;
class BgpServer;
class ClusterListDB;
class EdgeDiscoveryDB;
class EdgeForwardingDB;
class PmsiTunnelDB;

// BGP UPDATE attributes: as-path, community, ext-community, next-hop,
// cluster-list, ...
// all information in the UPDATE except: NLRI (prefix) and label.

struct BgpAttrOrigin : public BgpAttribute {
    static const int kSize = 1;
    static const uint8_t kFlags = Transitive;
    BgpAttrOrigin() : BgpAttribute(Origin, kFlags), origin(IGP) { }
    explicit BgpAttrOrigin(const BgpAttribute &rhs)
        : BgpAttribute(rhs), origin(IGP) {
    }
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
    explicit BgpAttrNextHop(const BgpAttribute &rhs)
        : BgpAttribute(rhs), nexthop(0) {
    }
    explicit BgpAttrNextHop(uint32_t nexthop)
        : BgpAttribute(NextHop, kFlags), nexthop(nexthop) {}
    explicit BgpAttrNextHop(Ip4Address v4_nexthop)
        : BgpAttribute(NextHop, kFlags), nexthop(v4_nexthop.to_ulong()) {}
    explicit BgpAttrNextHop(Ip6Address v6_nexthop)
        : BgpAttribute(NextHop, kFlags), nexthop(0), v6_nexthop(v6_nexthop) {}
    explicit BgpAttrNextHop(IpAddress ip_nexthop)
        : BgpAttribute(NextHop, kFlags), nexthop(0) {
        if (ip_nexthop.is_v4()) {
            nexthop = ip_nexthop.to_v4().to_ulong();
        } else if (ip_nexthop.is_v6()) {
            v6_nexthop = ip_nexthop.to_v6();
        }
    }
    uint32_t nexthop;
    Ip6Address v6_nexthop;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrMultiExitDisc : public BgpAttribute {
    static const int kSize = 4;
    static const uint8_t kFlags = Optional;
    BgpAttrMultiExitDisc() : BgpAttribute(MultiExitDisc, kFlags), med(0) {}
    explicit BgpAttrMultiExitDisc(const BgpAttribute &rhs)
        : BgpAttribute(rhs), med(0) {
    }
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
    explicit BgpAttrLocalPref(const BgpAttribute &rhs)
        : BgpAttribute(rhs), local_pref(0) {
    }
    explicit BgpAttrLocalPref(uint32_t local_pref)
        : BgpAttribute(LocalPref, kFlags), local_pref(local_pref) {
    }
    uint32_t local_pref;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrAtomicAggregate : public BgpAttribute {
    static const int kSize = 0;
    static const uint8_t kFlags = Transitive;
    BgpAttrAtomicAggregate() : BgpAttribute(AtomicAggregate, kFlags) {}
    explicit BgpAttrAtomicAggregate(const BgpAttribute &rhs)
        : BgpAttribute(rhs) {
    }
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrAggregator : public BgpAttribute {
    static const int kSize = 6;
    static const uint8_t kFlags = Optional|Transitive;
    BgpAttrAggregator()
        : BgpAttribute(Aggregator, kFlags), as_num(0), address(0) {
    }
    explicit BgpAttrAggregator(const BgpAttribute &rhs)
        : BgpAttribute(rhs), as_num(0), address(0) {
    }
    explicit BgpAttrAggregator(uint32_t as_num, uint32_t address) :
        BgpAttribute(Aggregator, kFlags), as_num(as_num), address(address) {}
    as_t as_num;
    uint32_t address;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrOriginatorId : public BgpAttribute {
    static const int kSize = 4;
    static const uint8_t kFlags = Optional;
    BgpAttrOriginatorId()
        : BgpAttribute(OriginatorId, kFlags), originator_id(0) {
    }
    explicit BgpAttrOriginatorId(const BgpAttribute &rhs)
        : BgpAttribute(rhs), originator_id(0) {
    }
    explicit BgpAttrOriginatorId(uint32_t originator_id)
        : BgpAttribute(OriginatorId, kFlags), originator_id(originator_id) {}
    uint32_t originator_id;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct ClusterListSpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Optional;
    ClusterListSpec() : BgpAttribute(ClusterList, kFlags) { }
    explicit ClusterListSpec(const BgpAttribute &rhs) : BgpAttribute(rhs) { }
    explicit ClusterListSpec(const ClusterListSpec &rhs)
        : BgpAttribute(BgpAttribute::ClusterList, kFlags) {
        cluster_list = rhs.cluster_list;
    }
    virtual int CompareTo(const BgpAttribute &rhs) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
    std::vector<uint32_t> cluster_list;
};

class ClusterList {
public:
    ClusterList(ClusterListDB *cluster_list_db, const ClusterListSpec &spec);
    ~ClusterList() { }
    void Remove();
    int CompareTo(const ClusterList &rhs) const {
        return spec_.CompareTo(rhs.cluster_list());
    }

    const ClusterListSpec &cluster_list() const { return spec_; }
    size_t size() const { return spec_.cluster_list.size(); }

    friend std::size_t hash_value(const ClusterList &cluster_list) {
        size_t hash = 0;
        return hash;
    }

private:
    friend int intrusive_ptr_add_ref(const ClusterList *ccluster_list);
    friend int intrusive_ptr_del_ref(const ClusterList *ccluster_list);
    friend void intrusive_ptr_release(const ClusterList *ccluster_list);
    friend class ClusterListDB;

    mutable tbb::atomic<int> refcount_;
    ClusterListDB *cluster_list_db_;
    ClusterListSpec spec_;
};

inline int intrusive_ptr_add_ref(const ClusterList *ccluster_list) {
    return ccluster_list->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const ClusterList *ccluster_list) {
    return ccluster_list->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const ClusterList *ccluster_list) {
    int prev = ccluster_list->refcount_.fetch_and_decrement();
    if (prev == 1) {
        ClusterList *cluster_list = const_cast<ClusterList *>(ccluster_list);
        cluster_list->Remove();
        assert(cluster_list->refcount_ == 0);
        delete cluster_list;
    }
}

typedef boost::intrusive_ptr<ClusterList> ClusterListPtr;

struct ClusterListCompare {
    bool operator()(const ClusterList *lhs, const ClusterList *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class ClusterListDB : public BgpPathAttributeDB<ClusterList, ClusterListPtr,
                                                ClusterListSpec,
                                                ClusterListCompare,
                                                ClusterListDB> {
public:
    explicit ClusterListDB(BgpServer *server);
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
    explicit BgpMpNlri(const BgpAttribute &rhs)
        : BgpAttribute(rhs), afi(0), safi(0) {
    }
    ~BgpMpNlri() {
        STLDeleteValues(&nlri);
    }
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual size_t EncodeLength() const;

    uint16_t afi;
    uint8_t safi;
    std::vector<uint8_t> nexthop;

    std::vector<BgpProtoPrefix *> nlri;
};

struct PmsiTunnelSpec : public BgpAttribute {
    enum Flags {
        LeafInfoRequired = 1 << 0,
        AssistedReplicationType = 3 << 3,
        EdgeReplicationSupported = 1 << 7
    };

    enum ARType {
        RegularNVE = 0 << 3,
        ARReplicator = 1 << 3,
        ARLeaf = 2 << 3
    };

    enum Type {
        NoTunnelInfo = 0,
        RsvpP2mpLsp = 1,
        LdpP2mpLsp = 2,
        PimSsmTree = 3,
        PimSmTree = 4,
        BidirPimTree = 5,
        IngressReplication = 6,
        MldpMp2mpLsp = 7,
        AssistedReplicationContrail = 252
    };

    static const int kSize = -1;
    static const uint8_t kFlags = Optional | Transitive;

    PmsiTunnelSpec();
    explicit PmsiTunnelSpec(const BgpAttribute &rhs);

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;

    uint32_t GetLabel(bool is_vni = false) const;
    void SetLabel(uint32_t label, bool is_vni = false);
    Ip4Address GetIdentifier() const;
    void SetIdentifier(Ip4Address identifier);

    std::string GetTunnelTypeString() const;
    std::string GetTunnelArTypeString() const;
    std::vector<std::string> GetTunnelFlagsStrings() const;

    uint8_t tunnel_flags;
    uint8_t tunnel_type;
    uint32_t label;
    std::vector<uint8_t> identifier;
};

class PmsiTunnel {
public:
    PmsiTunnel(PmsiTunnelDB *pmsi_tunnel_db, const PmsiTunnelSpec &pmsi_spec);
    virtual ~PmsiTunnel() { }
    virtual void Remove();
    int CompareTo(const PmsiTunnel &rhs) const {
        return pmsi_spec_.CompareTo(rhs.pmsi_tunnel());
    }

    const PmsiTunnelSpec &pmsi_tunnel() const { return pmsi_spec_; }
    uint32_t GetLabel(bool is_vni = false) const {
        return (is_vni ? label_ : label_ >> 4);
    }

    friend std::size_t hash_value(const PmsiTunnel &pmsi_tunnel) {
        size_t hash = 0;
        boost::hash_combine(hash, pmsi_tunnel.pmsi_tunnel().ToString());
        return hash;
    }

    const uint8_t tunnel_flags() const { return tunnel_flags_; }
    const uint8_t tunnel_type() const { return tunnel_type_; }
    const Ip4Address identifier() const { return identifier_; }
    const uint32_t label() const { return label_; }

private:
    friend int intrusive_ptr_add_ref(const PmsiTunnel *cpmsi_tunnel);
    friend int intrusive_ptr_del_ref(const PmsiTunnel *cpmsi_tunnel);
    friend void intrusive_ptr_release(const PmsiTunnel *cpmsi_tunnel);
    friend class PmsiTunnelDB;

    uint8_t tunnel_flags_;
    uint8_t tunnel_type_;
    Ip4Address identifier_;
    uint32_t label_;
    mutable tbb::atomic<int> refcount_;
    PmsiTunnelDB *pmsi_tunnel_db_;
    PmsiTunnelSpec pmsi_spec_;
};

inline int intrusive_ptr_add_ref(const PmsiTunnel *cpmsi_tunnel) {
    return cpmsi_tunnel->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const PmsiTunnel *cpmsi_tunnel) {
    return cpmsi_tunnel->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const PmsiTunnel *cpmsi_tunnel) {
    int prev = cpmsi_tunnel->refcount_.fetch_and_decrement();
    if (prev == 1) {
        PmsiTunnel *pmsi_tunnel = const_cast<PmsiTunnel *>(cpmsi_tunnel);
        pmsi_tunnel->Remove();
        assert(pmsi_tunnel->refcount_ == 0);
        delete pmsi_tunnel;
    }
}

typedef boost::intrusive_ptr<PmsiTunnel> PmsiTunnelPtr;

struct PmsiTunnelCompare {
    bool operator()(const PmsiTunnel *lhs, const PmsiTunnel *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class PmsiTunnelDB : public BgpPathAttributeDB<PmsiTunnel, PmsiTunnelPtr,
                                               PmsiTunnelSpec,
                                               PmsiTunnelCompare,
                                               PmsiTunnelDB> {
public:
    explicit PmsiTunnelDB(BgpServer *server);
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
    virtual size_t EncodeLength() const;

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
    EdgeDiscovery(EdgeDiscoveryDB *edge_discovery_db,
        const EdgeDiscoverySpec &edspec);
    virtual ~EdgeDiscovery();
    virtual void Remove();
    int CompareTo(const EdgeDiscovery &rhs) const;

    const EdgeDiscoverySpec &edge_discovery() const { return edspec_; }

    friend std::size_t hash_value(const EdgeDiscovery &edge_discovery) {
        size_t hash = 0;
        boost::hash_combine(hash, edge_discovery.edge_discovery().ToString());
        return hash;
    }

    struct Edge {
        explicit Edge(const EdgeDiscoverySpec::Edge *edge_spec);
        Ip4Address address;
        LabelBlockPtr label_block;
        bool operator<(const Edge &rhs) const;
    };
    typedef std::vector<Edge *> EdgeList;

    struct EdgeCompare {
        bool operator()(const Edge *lhs, const Edge *rhs) {
            BOOL_KEY_COMPARE(*lhs, *rhs);
            return false;
        }
    };

    EdgeList edge_list;

private:
    friend int intrusive_ptr_add_ref(const EdgeDiscovery *ediscovery);
    friend int intrusive_ptr_del_ref(const EdgeDiscovery *ediscovery);
    friend void intrusive_ptr_release(const EdgeDiscovery *ediscovery);
    friend class EdgeDiscoveryDB;

    mutable tbb::atomic<int> refcount_;
    EdgeDiscoveryDB *edge_discovery_db_;
    EdgeDiscoverySpec edspec_;
};

inline int intrusive_ptr_add_ref(const EdgeDiscovery *cediscovery) {
    return cediscovery->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const EdgeDiscovery *cediscovery) {
    return cediscovery->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const EdgeDiscovery *cediscovery) {
    int prev = cediscovery->refcount_.fetch_and_decrement();
    if (prev == 1) {
        EdgeDiscovery *ediscovery = const_cast<EdgeDiscovery *>(cediscovery);
        ediscovery->Remove();
        assert(ediscovery->refcount_ == 0);
        delete ediscovery;
    }
}

typedef boost::intrusive_ptr<EdgeDiscovery> EdgeDiscoveryPtr;

struct EdgeDiscoveryCompare {
    bool operator()(const EdgeDiscovery *lhs, const EdgeDiscovery *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class EdgeDiscoveryDB : public BgpPathAttributeDB<EdgeDiscovery,
                                                  EdgeDiscoveryPtr,
                                                  EdgeDiscoverySpec,
                                                  EdgeDiscoveryCompare,
                                                  EdgeDiscoveryDB> {
public:
    explicit EdgeDiscoveryDB(BgpServer *server);
};

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
    virtual size_t EncodeLength() const;

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
    EdgeForwarding(EdgeForwardingDB *edge_forwarding_db,
        const EdgeForwardingSpec &efspec);
    virtual ~EdgeForwarding();
    virtual void Remove();
    int CompareTo(const EdgeForwarding &rhs) const;

    const EdgeForwardingSpec &edge_forwarding() const { return efspec_; }

    friend std::size_t hash_value(const EdgeForwarding &edge_forwarding) {
        size_t hash = 0;
        boost::hash_combine(hash, edge_forwarding.edge_forwarding().ToString());
        return hash;
    }

    struct Edge {
        explicit Edge(const EdgeForwardingSpec::Edge *edge_spec);
        bool operator<(const Edge &rhs) const;
        Ip4Address inbound_address, outbound_address;
        uint32_t inbound_label, outbound_label;
    };
    typedef std::vector<Edge *> EdgeList;

    struct EdgeCompare {
        bool operator()(const Edge *lhs, const Edge *rhs) {
            BOOL_KEY_COMPARE(*lhs, *rhs);
            return false;
        }
    };

    EdgeList edge_list;

private:
    friend int intrusive_ptr_add_ref(const EdgeForwarding *ceforwarding);
    friend int intrusive_ptr_del_ref(const EdgeForwarding *ceforwarding);
    friend void intrusive_ptr_release(const EdgeForwarding *ceforwarding);
    friend class EdgeForwardingDB;

    mutable tbb::atomic<int> refcount_;
    EdgeForwardingDB *edge_forwarding_db_;
    EdgeForwardingSpec efspec_;
};

inline int intrusive_ptr_add_ref(const EdgeForwarding *ceforwarding) {
    return ceforwarding->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const EdgeForwarding *ceforwarding) {
    return ceforwarding->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const EdgeForwarding *ceforwarding) {
    int prev = ceforwarding->refcount_.fetch_and_decrement();
    if (prev == 1) {
        EdgeForwarding *eforwarding =
            const_cast<EdgeForwarding *>(ceforwarding);
        eforwarding->Remove();
        assert(eforwarding->refcount_ == 0);
        delete eforwarding;
    }
}

typedef boost::intrusive_ptr<EdgeForwarding> EdgeForwardingPtr;

struct EdgeForwardingCompare {
    bool operator()(const EdgeForwarding *lhs, const EdgeForwarding *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class EdgeForwardingDB : public BgpPathAttributeDB<EdgeForwarding,
                                                   EdgeForwardingPtr,
                                                   EdgeForwardingSpec,
                                                   EdgeForwardingCompare,
                                                   EdgeForwardingDB> {
public:
    explicit EdgeForwardingDB(BgpServer *server);
};

struct BgpAttrLabelBlock : public BgpAttribute {
    static const int kSize = 0;
    BgpAttrLabelBlock() : BgpAttribute(0, BgpAttribute::LabelBlock, 0) {}
    explicit BgpAttrLabelBlock(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    explicit BgpAttrLabelBlock(LabelBlockPtr label_block)
        : BgpAttribute(0, BgpAttribute::LabelBlock, 0),
          label_block(label_block) {
    }
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

    bool operator<(const BgpOListElem &rhs) const;

    Ip4Address address;
    uint32_t label;
    std::vector<std::string> encap;
};

struct BgpOListElemCompare {
    bool operator()(const BgpOListElem *lhs, const BgpOListElem *rhs) {
        BOOL_KEY_COMPARE(*lhs, *rhs);
        return false;
    }
};

struct BgpOListSpec : public BgpAttribute {
    static const int kSize = 0;
    BgpOListSpec() : BgpAttribute(0, BgpAttribute::OList, 0) {}
    explicit BgpOListSpec(uint8_t subcode) : BgpAttribute(0, subcode, 0) {}

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;

    typedef std::vector<BgpOListElem> Elements;
    Elements elements;
};

class BgpOList {
public:
    BgpOList(BgpOListDB *olist_db, const BgpOListSpec &olist_spec);
    virtual ~BgpOList();
    virtual void Remove();
    int CompareTo(const BgpOList &rhs) const;

    const BgpOListSpec &olist() const { return olist_spec_; }

    friend std::size_t hash_value(const BgpOList &olist) {
        size_t hash = 0;
        boost::hash_combine(hash, olist.olist().ToString());
        return hash;
    }

    typedef std::vector<BgpOListElem *> Elements;

    const Elements &elements() const { return elements_; }

private:
    friend int intrusive_ptr_add_ref(const BgpOList *colist);
    friend int intrusive_ptr_del_ref(const BgpOList *colist);
    friend void intrusive_ptr_release(const BgpOList *colist);
    friend class BgpOListDB;

    Elements elements_;
    mutable tbb::atomic<int> refcount_;
    BgpOListDB *olist_db_;
    BgpOListSpec olist_spec_;
};

inline int intrusive_ptr_add_ref(const BgpOList *colist) {
    return colist->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const BgpOList *colist) {
    return colist->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const BgpOList *colist) {
    int prev = colist->refcount_.fetch_and_decrement();
    if (prev == 1) {
        BgpOList *olist = const_cast<BgpOList *>(colist);
        olist->Remove();
        assert(olist->refcount_ == 0);
        delete olist;
    }
}

typedef boost::intrusive_ptr<BgpOList> BgpOListPtr;

struct BgpOListCompare {
    bool operator()(const BgpOList *lhs, const BgpOList *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class BgpOListDB : public BgpPathAttributeDB<BgpOList,
                                             BgpOListPtr,
                                             BgpOListSpec,
                                             BgpOListCompare,
                                             BgpOListDB> {
public:
    explicit BgpOListDB(BgpServer *server);
};

struct BgpAttrUnknown : public BgpAttribute {
    BgpAttrUnknown() : BgpAttribute() {}
    explicit BgpAttrUnknown(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    std::vector<uint8_t> value;
};

struct BgpAttrSourceRd : public BgpAttribute {
    BgpAttrSourceRd() : BgpAttribute(0, SourceRd, 0) {}
    explicit BgpAttrSourceRd(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    explicit BgpAttrSourceRd(const RouteDistinguisher &source_rd)
        : BgpAttribute(0, SourceRd, 0), source_rd(source_rd) {
    }
    RouteDistinguisher source_rd;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrEsi : public BgpAttribute {
    BgpAttrEsi() : BgpAttribute(0, Esi, 0) {}
    explicit BgpAttrEsi(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    explicit BgpAttrEsi(const EthernetSegmentId &esi)
        : BgpAttribute(0, Esi, 0), esi(esi) {
    }
    EthernetSegmentId esi;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

struct BgpAttrParams : public BgpAttribute {
    enum Flags {
        TestFlag = 1 << 0
    };

    BgpAttrParams() : BgpAttribute(0, Params, 0), params(0) {}
    explicit BgpAttrParams(const BgpAttribute &rhs)
        : BgpAttribute(rhs), params(0) {
    }
    explicit BgpAttrParams(uint64_t params) :
            BgpAttribute(0, Params, 0), params(params) {}
    uint64_t params;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

typedef std::vector<BgpAttribute *> BgpAttrSpec;

// Canonicalized BGP attribute
class BgpAttr {
public:
    BgpAttr();
    explicit BgpAttr(BgpAttrDB *attr_db);
    explicit BgpAttr(const BgpAttr &rhs);
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
    void set_originator_id(Ip4Address originator_id) {
        originator_id_ = originator_id;
    }
    void set_source_rd(const RouteDistinguisher &source_rd) {
        source_rd_ = source_rd;
    }
    void set_esi(const EthernetSegmentId &esi) { esi_ = esi; }
    void set_params(uint64_t params) { params_ = params; }
    void set_as_path(AsPathPtr aspath);
    void set_as_path(const AsPathSpec *spec);
    void set_cluster_list(const ClusterListSpec *spec);
    void set_community(CommunityPtr comm);
    void set_community(const CommunitySpec *comm);
    void set_ext_community(ExtCommunityPtr extcomm);
    void set_ext_community(const ExtCommunitySpec *extcomm);
    void set_origin_vn_path(OriginVnPathPtr ovnpath);
    void set_origin_vn_path(const OriginVnPathSpec *spec);
    void set_pmsi_tunnel(const PmsiTunnelSpec *pmsi_spec);
    void set_edge_discovery(const EdgeDiscoverySpec *edspec);
    void set_edge_forwarding(const EdgeForwardingSpec *efspec);
    void set_label_block(LabelBlockPtr label_block);
    void set_olist(const BgpOListSpec *olist_spec);
    void set_leaf_olist(const BgpOListSpec *leaf_olist_spec);
    friend std::size_t hash_value(BgpAttr const &attr);

    BgpAttrOrigin::OriginType origin() const { return origin_; }
    const IpAddress &nexthop() const { return nexthop_; }
    Address::Family nexthop_family() const;
    uint32_t med() const { return med_; }
    uint32_t local_pref() const { return local_pref_; }
    bool atomic_aggregate() const { return atomic_aggregate_; }
    as_t aggregator_as_num() const { return aggregator_as_num_; }
    as_t neighbor_as() const;
    const IpAddress &aggregator_adderess() const { return aggregator_address_; }
    const Ip4Address &originator_id() const { return originator_id_; }
    const RouteDistinguisher &source_rd() const { return source_rd_; }
    const EthernetSegmentId &esi() const { return esi_; }
    uint64_t params() const { return params_; }
    const AsPath *as_path() const { return as_path_.get(); }
    int as_path_count() const { return as_path_ ? as_path_->AsCount() : 0; }
    const ClusterList *cluster_list() const { return cluster_list_.get(); }
    size_t cluster_list_length() const {
        return cluster_list_ ? cluster_list_->size() : 0;
    }
    const Community *community() const { return community_.get(); }
    const ExtCommunity *ext_community() const { return ext_community_.get(); }
    const OriginVnPath *origin_vn_path() const { return origin_vn_path_.get(); }
    const PmsiTunnel *pmsi_tunnel() const { return pmsi_tunnel_.get(); }
    const EdgeDiscovery *edge_discovery() const {
        return edge_discovery_.get();
    }
    const EdgeForwarding *edge_forwarding() const {
        return edge_forwarding_.get();
    }
    LabelBlockPtr label_block() const { return label_block_; }
    BgpOListPtr olist() const { return olist_; }
    BgpOListPtr leaf_olist() const { return leaf_olist_; }
    BgpAttrDB *attr_db() const { return attr_db_; }
    uint32_t sequence_number() const;

private:
    friend class BgpAttrDB;
    friend class BgpAttrTest;
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
    Ip4Address originator_id_;
    RouteDistinguisher source_rd_;
    EthernetSegmentId esi_;
    uint64_t params_;
    AsPathPtr as_path_;
    ClusterListPtr cluster_list_;
    CommunityPtr community_;
    ExtCommunityPtr ext_community_;
    OriginVnPathPtr origin_vn_path_;
    PmsiTunnelPtr pmsi_tunnel_;
    EdgeDiscoveryPtr edge_discovery_;
    EdgeForwardingPtr edge_forwarding_;
    LabelBlockPtr label_block_;
    BgpOListPtr olist_;
    BgpOListPtr leaf_olist_;
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
    explicit BgpAttrDB(BgpServer *server);
    BgpAttrPtr ReplaceAsPathAndLocate(const BgpAttr *attr,
                                      AsPathPtr aspath);
    BgpAttrPtr ReplaceCommunityAndLocate(const BgpAttr *attr,
                                         CommunityPtr community);
    BgpAttrPtr ReplaceExtCommunityAndLocate(const BgpAttr *attr,
                                            ExtCommunityPtr extcomm);
    BgpAttrPtr ReplaceOriginVnPathAndLocate(const BgpAttr *attr,
                                            OriginVnPathPtr ovnpath);
    BgpAttrPtr ReplaceLocalPreferenceAndLocate(const BgpAttr *attr,
                                               uint32_t local_pref);
    BgpAttrPtr ReplaceOriginatorIdAndLocate(const BgpAttr *attr,
                                            Ip4Address originator_id);
    BgpAttrPtr ReplaceSourceRdAndLocate(const BgpAttr *attr,
                                        const RouteDistinguisher &source_rd);
    BgpAttrPtr ReplaceEsiAndLocate(const BgpAttr *attr,
                                   const EthernetSegmentId &esi);
    BgpAttrPtr ReplaceOListAndLocate(const BgpAttr *attr,
                                     const BgpOListSpec *olist_spec);
    BgpAttrPtr ReplaceLeafOListAndLocate(const BgpAttr *attr,
                                         const BgpOListSpec *leaf_olist_spec);
    BgpAttrPtr ReplacePmsiTunnelAndLocate(const BgpAttr *attr,
                                          const PmsiTunnelSpec *pmsi_spec);
    BgpAttrPtr ReplaceNexthopAndLocate(const BgpAttr *attr,
                                       const IpAddress &addr);
    BgpServer *server() { return server_; }

private:
    BgpServer *server_;
};

#endif  // SRC_BGP_BGP_ATTR_H_
