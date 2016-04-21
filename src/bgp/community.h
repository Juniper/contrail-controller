/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_COMMUNITY_H_
#define SRC_BGP_COMMUNITY_H_

#include <boost/array.hpp>
#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

#include <set>
#include <string>
#include <vector>

#include "base/parse_object.h"
#include "base/util.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/extended-community/types.h"

class BgpAttr;
class CommunityDB;
class ExtCommunityDB;
class BgpServer;

struct CommunitySpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Optional | Transitive;
    CommunitySpec() : BgpAttribute(Communities, kFlags) { }
    explicit CommunitySpec(const BgpAttribute &rhs) : BgpAttribute(rhs) { }
    std::vector<uint32_t> communities;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const {
        int ret = BgpAttribute::CompareTo(rhs_attr);
        if (ret != 0) return ret;
        KEY_COMPARE(communities,
                    static_cast<const CommunitySpec &>(rhs_attr).communities);
        return 0;
    }
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
    virtual size_t EncodeLength() const;
};

class Community {
public:
    typedef std::vector<uint32_t> CommunityList;
    explicit Community(CommunityDB *comm_db)
        : comm_db_(comm_db) {
        refcount_ = 0;
    }
    explicit Community(const Community &rhs)
        : comm_db_(rhs.comm_db_), communities_(rhs.communities_) {
        refcount_ = 0;
    }
    explicit Community(CommunityDB *comm_db, const CommunitySpec spec);
    virtual ~Community() { }
    virtual void Remove();

    int CompareTo(const Community &rhs) const;
    bool ContainsValue(uint32_t value) const;
    void BuildStringList(std::vector<std::string> *list) const;

    const std::vector<uint32_t> &communities() const { return communities_; }

    friend std::size_t hash_value(Community const &comm) {
        size_t hash = 0;
        boost::hash_range(hash, comm.communities_.begin(),
                          comm.communities_.end());
        return hash;
    }

private:
    friend int intrusive_ptr_add_ref(const Community *ccomm);
    friend int intrusive_ptr_del_ref(const Community *ccomm);
    friend void intrusive_ptr_release(const Community *ccomm);
    friend class CommunityDB;
    friend class BgpAttrTest;

    void Append(uint32_t value);
    void Append(const std::vector<uint32_t> &communities);
    void Set(const std::vector<uint32_t> &communities);
    void Remove(const std::vector<uint32_t> &communities);

    mutable tbb::atomic<int> refcount_;
    CommunityDB *comm_db_;
    std::vector<uint32_t> communities_;
};

inline int intrusive_ptr_add_ref(const Community *ccomm) {
    return ccomm->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const Community *ccomm) {
    return ccomm->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const Community *ccomm) {
    int prev = ccomm->refcount_.fetch_and_decrement();
    if (prev == 1) {
        Community *comm = const_cast<Community *>(ccomm);
        comm->Remove();
        assert(comm->refcount_ == 0);
        delete comm;
    }
}

typedef boost::intrusive_ptr<const Community> CommunityPtr;

struct CommunityCompare {
    bool operator()(const Community *lhs, const Community *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class CommunityDB : public BgpPathAttributeDB<Community, CommunityPtr,
                                              CommunitySpec, CommunityCompare,
                                              CommunityDB> {
public:
    explicit CommunityDB(BgpServer *server);
    virtual ~CommunityDB() { }

    CommunityPtr AppendAndLocate(const Community *src, uint32_t value);
    CommunityPtr AppendAndLocate(const Community *src,
                                 const std::vector<uint32_t> &value);
    CommunityPtr SetAndLocate(const Community *src,
                              const std::vector<uint32_t> &value);
    CommunityPtr RemoveAndLocate(const Community *src,
                                 const std::vector<uint32_t> &value);
    CommunityPtr RemoveAndLocate(const Community *src, uint32_t value);

private:
};

class ExtCommunitySpec : public BgpAttribute {
public:
    static const int kSize = -1;
    static const uint8_t kFlags = Optional | Transitive;
    ExtCommunitySpec() : BgpAttribute(ExtendedCommunities, kFlags) { }
    explicit ExtCommunitySpec(const BgpAttribute &rhs) : BgpAttribute(rhs) { }
    virtual size_t EncodeLength() const;
    std::vector<uint64_t> communities;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
};

class ExtCommunity {
public:
    typedef boost::array<uint8_t, 8> ExtCommunityValue;
    typedef std::vector<ExtCommunityValue> ExtCommunityList;

    explicit ExtCommunity(ExtCommunityDB *extcomm_db)
        : extcomm_db_(extcomm_db) {
        refcount_ = 0;
    }
    explicit ExtCommunity(const ExtCommunity &rhs)
        : extcomm_db_(rhs.extcomm_db_),
          communities_(rhs.communities_) {
        refcount_ = 0;
    }

    explicit ExtCommunity(ExtCommunityDB *extcomm_db,
                          const ExtCommunitySpec spec);
    virtual ~ExtCommunity() { }
    virtual void Remove();
    int CompareTo(const ExtCommunity &rhs) const;

    bool ContainsOriginVn(const ExtCommunityValue &val) const;

    // Return vector of communities
    const ExtCommunityList &communities() const {
        return communities_;
    }

    std::vector<std::string> GetTunnelEncap() const;
    bool ContainsTunnelEncapVxlan() const;
    int GetOriginVnIndex() const;

    static bool is_origin_vn(const ExtCommunityValue &val) {
        //
        // Origin VN extended community
        // 2 Octet AS specific extended community
        //
        return (val[0] == BgpExtendedCommunityType::Experimental) &&
               (val[1] == BgpExtendedCommunityExperimentalSubType::OriginVn);
    }

    static bool is_default_gateway(const ExtCommunityValue &val) {
        //
        // Default Gateway extended community
        //
        return (val[0] == BgpExtendedCommunityType::Opaque) &&
               (val[1] == BgpExtendedCommunityOpaqueSubType::DefaultGateway);
    }

    static bool is_es_import(const ExtCommunityValue &val) {
        //
        // ES Import extended community
        //
        return (val[0] == BgpExtendedCommunityType::Evpn) &&
               (val[1] == BgpExtendedCommunityEvpnSubType::EsImport);
    }

    static bool is_esi_label(const ExtCommunityValue &val) {
        //
        // ESI Label extended community
        //
        return (val[0] == BgpExtendedCommunityType::Evpn) &&
               (val[1] == BgpExtendedCommunityEvpnSubType::EsiMplsLabel);
    }

    static bool is_mac_mobility(const ExtCommunityValue &val) {
        //
        // MAC Mobility extended community
        //
        return (val[0] == BgpExtendedCommunityType::Evpn) &&
               (val[1] == BgpExtendedCommunityEvpnSubType::MacMobility);
    }

    static bool is_route_target(const ExtCommunityValue &val) {
        //
        // Route target extended community
        // 1. 2 Octet AS specific extended community Route Target
        // 2. IPv4 Address specific extended community Route Target
        // 3. 4 Octet AS specific extended community Route Target
        //
        return ((val[0] == BgpExtendedCommunityType::TwoOctetAS ||
                (val[0] == BgpExtendedCommunityType::FourOctetAS) ||
                (val[0] == BgpExtendedCommunityType::IPv4Address)) &&
                (val[1] == BgpExtendedCommunitySubType::RouteTarget));
    }

    static bool is_security_group(const ExtCommunityValue &val) {
        //
        // SG ID extended community
        // 2 Octet AS specific extended community
        //
        return (val[0] == BgpExtendedCommunityType::Experimental) &&
               (val[1] == BgpExtendedCommunityExperimentalSubType::SgId);
    }

    static bool is_site_of_origin(const ExtCommunityValue &val) {
        //
        // Site of Origin / Route Origin extended community
        // 1. 2 Octet AS specific extended community
        // 2. IPv4 Address specific extended community
        // 3. 4 Octet AS specific extended community Route Target
        //
        return ((val[0] == BgpExtendedCommunityType::TwoOctetAS ||
                (val[0] == BgpExtendedCommunityType::FourOctetAS) ||
                (val[0] == BgpExtendedCommunityType::IPv4Address)) &&
                (val[1] == BgpExtendedCommunitySubType::RouteOrigin));
    }

    static bool is_tunnel_encap(const ExtCommunityValue &val) {
        // Tunnel encap extended community
        return (val[0] == BgpExtendedCommunityType::Opaque) &&
               (val[1] == BgpExtendedCommunityOpaqueSubType::TunnelEncap);
    }

    static bool is_load_balance(const ExtCommunityValue &val) {
        // Load Balance extended community
        return (val[0] == BgpExtendedCommunityType::Opaque) &&
               (val[1] == BgpExtendedCommunityOpaqueSubType::LoadBalance);
    }

    friend std::size_t hash_value(ExtCommunity const &comm) {
        size_t hash = 0;
        for (ExtCommunityList::const_iterator iter = comm.communities_.begin();
                iter != comm.communities_.end(); iter++) {
            boost::hash_range(hash, iter->begin(), iter->end());
        }
        return hash;
    }

private:
    friend int intrusive_ptr_add_ref(const ExtCommunity *cextcomm);
    friend int intrusive_ptr_del_ref(const ExtCommunity *cextcomm);
    friend void intrusive_ptr_release(const ExtCommunity *cextcomm);
    friend class ExtCommunityDB;
    friend class BgpAttrTest;

    void Append(const ExtCommunityValue &value);
    void Append(const ExtCommunityList &list);
    void RemoveRTarget();
    void RemoveSGID();
    void RemoveSiteOfOrigin();
    void RemoveOriginVn();
    void RemoveTunnelEncapsulation();
    void RemoveLoadBalance();

    mutable tbb::atomic<int> refcount_;
    ExtCommunityDB *extcomm_db_;
    ExtCommunityList communities_;
};

inline int intrusive_ptr_add_ref(const ExtCommunity *cextcomm) {
    return cextcomm->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const ExtCommunity *cextcomm) {
    return cextcomm->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const ExtCommunity *cextcomm) {
    int prev = cextcomm->refcount_.fetch_and_decrement();
    if (prev == 1) {
        ExtCommunity *extcomm = const_cast<ExtCommunity *>(cextcomm);
        extcomm->Remove();
        assert(extcomm->refcount_ == 0);
        delete extcomm;
    }
}

typedef boost::intrusive_ptr<const ExtCommunity> ExtCommunityPtr;

struct ExtCommunityCompare {
    bool operator()(const ExtCommunity *lhs, const ExtCommunity *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class ExtCommunityDB : public BgpPathAttributeDB<ExtCommunity, ExtCommunityPtr,
                                                 ExtCommunitySpec,
                                                 ExtCommunityCompare,
                                                 ExtCommunityDB> {
public:
    explicit ExtCommunityDB(BgpServer *server);

    ExtCommunityPtr AppendAndLocate(const ExtCommunity *src,
            const ExtCommunity::ExtCommunityList &list);
    ExtCommunityPtr AppendAndLocate(const ExtCommunity *src,
            const ExtCommunity::ExtCommunityValue &value);

    ExtCommunityPtr ReplaceRTargetAndLocate(const ExtCommunity *src,
            const ExtCommunity::ExtCommunityList &export_list);
    ExtCommunityPtr ReplaceSGIDListAndLocate(const ExtCommunity *src,
            const ExtCommunity::ExtCommunityList &sgid_list);
    ExtCommunityPtr RemoveSiteOfOriginAndLocate(const ExtCommunity *src);
    ExtCommunityPtr ReplaceSiteOfOriginAndLocate(const ExtCommunity *src,
            const ExtCommunity::ExtCommunityValue &soo);
    ExtCommunityPtr RemoveOriginVnAndLocate(const ExtCommunity *src);
    ExtCommunityPtr ReplaceOriginVnAndLocate(const ExtCommunity *src,
            const ExtCommunity::ExtCommunityValue &origin_vn);
    ExtCommunityPtr ReplaceTunnelEncapsulationAndLocate(
            const ExtCommunity *src,
            const ExtCommunity::ExtCommunityList &tunnel_encaps);
    ExtCommunityPtr ReplaceLoadBalanceAndLocate(const ExtCommunity *src,
            const ExtCommunity::ExtCommunityValue &lb);

private:
};

#endif  // SRC_BGP_COMMUNITY_H_
