/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_ORIGIN_VN_PATH_H_
#define SRC_BGP_BGP_ORIGIN_VN_PATH_H_

#include <array>
#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

#include <set>
#include <string>
#include <vector>

#include "base/parse_object.h"
#include "base/util.h"
#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_common.h"

class BgpAttr;
class OriginVnPathDB;
class BgpServer;

struct OriginVnPathSpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Optional | Transitive;
    OriginVnPathSpec() : BgpAttribute(OriginVnPath, kFlags) { }
    explicit OriginVnPathSpec(const BgpAttribute &rhs) : BgpAttribute(rhs) { }
    std::vector<uint64_t> origin_vns;
    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;
    virtual size_t EncodeLength() const;
};

class OriginVnPath {
public:
 typedef std::array<uint8_t, 8> OriginVnValue;
 typedef std::vector<OriginVnValue> OriginVnList;

 explicit OriginVnPath(OriginVnPathDB *ovnpath_db) : ovnpath_db_(ovnpath_db) {
     refcount_ = 0;
    }
    explicit OriginVnPath(const OriginVnPath &rhs)
        : ovnpath_db_(rhs.ovnpath_db_),
          origin_vns_(rhs.origin_vns_) {
        refcount_ = 0;
    }
    explicit OriginVnPath(OriginVnPathDB *ovnpath_db,
                          const OriginVnPathSpec spec);
    virtual ~OriginVnPath() { }
    virtual void Remove();

    bool Contains(const OriginVnValue &value) const;
    bool Contains(as_t asn, uint32_t vn_index) const;
    int CompareTo(const OriginVnPath &rhs) const;

    const OriginVnList &origin_vns() const { return origin_vns_; }

    friend std::size_t hash_value(const OriginVnPath &ovnpath) {
        size_t hash = 0;
        for (OriginVnList::const_iterator it = ovnpath.origin_vns_.begin();
             it != ovnpath.origin_vns_.end(); ++it) {
            boost::hash_range(hash, it->begin(), it->end());
        }
        return hash;
    }

private:
    friend int intrusive_ptr_add_ref(const OriginVnPath *covnpath);
    friend int intrusive_ptr_del_ref(const OriginVnPath *covnpath);
    friend void intrusive_ptr_release(const OriginVnPath *covnpath);
    friend class OriginVnPathDB;
    friend class BgpAttrTest;

    void Prepend(const OriginVnValue &value);

    mutable tbb::atomic<int> refcount_;
    OriginVnPathDB *ovnpath_db_;
    OriginVnList origin_vns_;
};

inline int intrusive_ptr_add_ref(const OriginVnPath *covnpath) {
    return covnpath->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const OriginVnPath *covnpath) {
    return covnpath->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const OriginVnPath *covnpath) {
    int prev = covnpath->refcount_.fetch_and_decrement();
    if (prev == 1) {
        OriginVnPath *ovnpath = const_cast<OriginVnPath *>(covnpath);
        ovnpath->Remove();
        assert(ovnpath->refcount_ == 0);
        delete ovnpath;
    }
}

typedef boost::intrusive_ptr<const OriginVnPath> OriginVnPathPtr;

struct OriginVnPathCompare {
    bool operator()(const OriginVnPath *lhs, const OriginVnPath *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class OriginVnPathDB : public BgpPathAttributeDB<OriginVnPath, OriginVnPathPtr,
                                                 OriginVnPathSpec,
                                                 OriginVnPathCompare,
                                                 OriginVnPathDB> {
public:
    explicit OriginVnPathDB(BgpServer *server);
    OriginVnPathPtr PrependAndLocate(const OriginVnPath *ovnpath,
        const OriginVnPath::OriginVnValue &value);

private:
    DISALLOW_COPY_AND_ASSIGN(OriginVnPathDB);
};

#endif  // SRC_BGP_BGP_ORIGIN_VN_PATH_H_
