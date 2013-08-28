/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_origin_vn_h
#define ctrlplane_bgp_origin_vn_h

#include <set>
#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

#include "bgp/bgp_attr_base.h"
#include "base/util.h"

class BgpAttr;
class OriginVnDB;
class BgpServer;

struct OriginVnSpec : public BgpAttribute {
    static const int kSize = 0;
    OriginVnSpec() : BgpAttribute(0, BgpAttribute::OriginVn, 0) {}
    OriginVnSpec(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    OriginVnSpec(const OriginVnSpec &rhs) :
        BgpAttribute(0, BgpAttribute::OriginVn, 0), origin_vn(rhs.origin_vn) {
    }
    OriginVnSpec(std::string origin_vn) :
        BgpAttribute(0, BgpAttribute::OriginVn, 0), origin_vn(origin_vn) {}

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual std::string ToString() const;

    std::string origin_vn;
};

class OriginVn {
public:
    OriginVn(OriginVnDB *origin_vn_db) : origin_vn_db_(origin_vn_db) {
        refcount_ = 0;
    }
    explicit OriginVn(OriginVnDB *origin_vn_db, const OriginVnSpec &origin_vn)
        : origin_vn_db_(origin_vn_db), origin_vn_(origin_vn) {
        refcount_ = 0;
    }

    virtual ~OriginVn() { }
    virtual void Remove();
    int CompareTo(const OriginVn &rhs) const {
        KEY_COMPARE(origin_vn_.origin_vn, rhs.origin_vn_.origin_vn);
        return 0;
    }

    const OriginVnSpec &origin_vn() const { return origin_vn_; }

    friend std::size_t hash_value(OriginVn const &origin_vn) {
        size_t hash = 0;
        boost::hash_combine(hash, origin_vn.origin_vn().ToString());
        return hash;
    }

private:
    friend int intrusive_ptr_add_ref(const OriginVn *origin_vn);
    friend int intrusive_ptr_del_ref(const OriginVn *origin_vn);
    friend void intrusive_ptr_release(OriginVn *origin_vn);

    mutable tbb::atomic<int> refcount_;
    OriginVnDB *origin_vn_db_;
    OriginVnSpec origin_vn_;
};

inline int intrusive_ptr_add_ref(const OriginVn *origin_vn) {
    return origin_vn->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const OriginVn *origin_vn) {
    return origin_vn->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(OriginVn *origin_vn) {
    int prev = origin_vn->refcount_.fetch_and_decrement();
    if (prev == 1) {
        origin_vn->Remove();
        assert(origin_vn->refcount_ == 0);
        delete origin_vn;
    }
}

typedef boost::intrusive_ptr<OriginVn> OriginVnPtr;

struct OriginVnCompare {
    bool operator()(const OriginVn *lhs, const OriginVn *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class OriginVnDB : public BgpPathAttributeDB<OriginVn, OriginVnPtr,
                                             OriginVnSpec, OriginVnCompare,
                                             OriginVnDB> {
public:
    OriginVnDB(BgpServer *server);

private:
    BgpServer *server_;
};

#endif
