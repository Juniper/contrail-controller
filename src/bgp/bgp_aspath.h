/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_ASPATH_H_
#define SRC_BGP_BGP_ASPATH_H_

#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "bgp/bgp_attr_base.h"
#include "bgp/bgp_common.h"
#include "base/parse_object.h"
#include "base/util.h"

class BgpAttr;
class AsPathDB;
class BgpServer;

struct AsPathSpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Transitive;
    AsPathSpec() : BgpAttribute(BgpAttribute::AsPath, kFlags) {}
    explicit AsPathSpec(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    explicit AsPathSpec(const AsPathSpec &rhs) :
            BgpAttribute(BgpAttribute::AsPath, kFlags) {
        for (size_t i = 0; i < rhs.path_segments.size(); i++) {
            PathSegment *ps = new PathSegment;
            *ps = *rhs.path_segments[i];
            path_segments.push_back(ps);
        }
    }
    ~AsPathSpec() {
        STLDeleteValues(&path_segments);
    }
    struct PathSegment : public ParseObject {
        int CompareTo(const PathSegment &rhs) const {
            KEY_COMPARE(path_segment_type, rhs.path_segment_type);
            KEY_COMPARE(path_segment, rhs.path_segment);
            return 0;
        }

        enum PathSegmentType {
            AS_SET = 1,
            AS_SEQUENCE = 2
        };
        int path_segment_type;
        std::vector<as_t> path_segment;
    };

    as_t AsLeftMost() const;
    bool AsLeftMostMatch(as_t as) const;
    bool AsPathLoop(as_t as, uint8_t max_loop_count = 0) const;

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual size_t EncodeLength() const;
    virtual std::string ToString() const;
    AsPathSpec *Add(as_t asn) const;
    AsPathSpec *Replace(as_t old_asn, as_t asn) const;
    std::vector<PathSegment *> path_segments;
};

class AsPath {
public:
    explicit AsPath(AsPathDB *aspath_db) : aspath_db_(aspath_db) {
        refcount_ = 0;
    }
    explicit AsPath(AsPathDB *aspath_db, const AsPathSpec &spec)
        : aspath_db_(aspath_db), path_(spec) {
        refcount_ = 0;
        for (size_t i = 0; i < path_.path_segments.size(); i++) {
            AsPathSpec::PathSegment *ps = path_.path_segments[i];
            if (ps->path_segment_type == AsPathSpec::PathSegment::AS_SET) {
                std::sort(ps->path_segment.begin(), ps->path_segment.end());
            }
        }
    }
    virtual ~AsPath() { }
    virtual void Remove();
    int AsCount() const {
        int count = 0;
        std::vector<AsPathSpec::PathSegment *>::const_iterator i;
        for (i = path_.path_segments.begin(); i < path_.path_segments.end();
                i++) {
            if ((*i)->path_segment_type == AsPathSpec::PathSegment::AS_SET) {
                count++;
            } else {
                count += (*i)->path_segment.size();
            }
        }
        return count;
    }

    int CompareTo(const AsPath &rhs) const {
        const std::vector<AsPathSpec::PathSegment *> &lps = path_.path_segments;
        const std::vector<AsPathSpec::PathSegment *> &rps =
                rhs.path_.path_segments;

        KEY_COMPARE(lps.size(), rps.size());

        std::vector<AsPathSpec::PathSegment *>::const_iterator i, j;
        for (i = lps.begin(), j = rps.begin(); i < lps.end(); i++, j++) {
            int ret = (*i)->CompareTo(**j);
            if (ret != 0) return ret;
        }
        return 0;
    }
    const AsPathSpec &path() const { return path_; }
    bool empty() const { return path_.path_segments.empty(); }
    as_t neighbor_as() const { return path_.AsLeftMost(); }

    friend std::size_t hash_value(AsPath const &as_path) {
        size_t hash = 0;
        boost::hash_combine(hash, as_path.path().ToString());
        return hash;
    }

private:
    friend int intrusive_ptr_add_ref(const AsPath *cpath);
    friend int intrusive_ptr_del_ref(const AsPath *cpath);
    friend void intrusive_ptr_release(const AsPath *cpath);

    mutable tbb::atomic<int> refcount_;
    AsPathDB *aspath_db_;
    AsPathSpec path_;
};

inline int intrusive_ptr_add_ref(const AsPath *cpath) {
    return cpath->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const AsPath *cpath) {
    return cpath->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const AsPath *cpath) {
    int prev = cpath->refcount_.fetch_and_decrement();
    if (prev == 1) {
        AsPath *path = const_cast<AsPath *>(cpath);
        path->Remove();
        assert(path->refcount_ == 0);
        delete path;
    }
}

typedef boost::intrusive_ptr<const AsPath> AsPathPtr;

struct AsPathCompare {
    bool operator()(const AsPath *lhs, const AsPath *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class AsPathDB : public BgpPathAttributeDB<AsPath, AsPathPtr, AsPathSpec,
                                           AsPathCompare, AsPathDB> {
public:
    explicit AsPathDB(BgpServer *server);

private:
};

#endif  // SRC_BGP_BGP_ASPATH_H_
