/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_AS4PATH_H_
#define SRC_BGP_BGP_AS4PATH_H_

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
class As4PathDB;
class BgpServer;

struct As4PathSpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Transitive|Optional;
    static const as4_t kMinPrivateAs = 64512;
    static const as4_t kMaxPrivateAs = 65535;
    static const as4_t kMinPrivateAs4 = 4200000000;
    static const as4_t kMaxPrivateAs4 = 4294967294;

    As4PathSpec() : BgpAttribute(BgpAttribute::As4Path, kFlags) {}
    explicit As4PathSpec(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    explicit As4PathSpec(const As4PathSpec &rhs) :
            BgpAttribute(BgpAttribute::As4Path, kFlags) {
        for (size_t i = 0; i < rhs.path_segments.size(); i++) {
            PathSegment *ps = new PathSegment;
            *ps = *rhs.path_segments[i];
            path_segments.push_back(ps);
        }
    }
    ~As4PathSpec() {
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
        std::vector<as4_t> path_segment;
    };

    as4_t AsLeftMost() const;
    bool AsLeftMostMatch(as4_t as) const;
    as4_t AsLeftMostPublic() const;
    bool AsPathLoop(as4_t as, uint8_t max_loop_count = 0) const;
    static bool AsIsPrivate(as4_t as) {
        return ((as >= kMinPrivateAs && as <= kMaxPrivateAs) ||
            (as >= kMinPrivateAs4 && as <= kMaxPrivateAs4));
    }

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual size_t EncodeLength() const;
    virtual std::string ToString() const;
    As4PathSpec *Add(as4_t asn) const;
    As4PathSpec *Add(const std::vector<as4_t> &asn_list) const;
    As4PathSpec *Replace(as4_t old_asn, as4_t asn) const;
    As4PathSpec *RemovePrivate(bool all, as4_t asn, as4_t peer_as) const;

    std::vector<PathSegment *> path_segments;
};

class As4Path {
public:
    explicit As4Path(As4PathDB *aspath_db) : aspath_db_(aspath_db) {
        refcount_ = 0;
    }
    explicit As4Path(As4PathDB *aspath_db, const As4PathSpec &spec)
        : aspath_db_(aspath_db), path_(spec) {
        refcount_ = 0;
        for (size_t i = 0; i < path_.path_segments.size(); i++) {
            As4PathSpec::PathSegment *ps = path_.path_segments[i];
            if (ps->path_segment_type == As4PathSpec::PathSegment::AS_SET) {
                std::sort(ps->path_segment.begin(), ps->path_segment.end());
            }
        }
    }
    virtual ~As4Path() { }
    virtual void Remove();
    int AsCount() const {
        int count = 0;
        std::vector<As4PathSpec::PathSegment *>::const_iterator i;
        for (i = path_.path_segments.begin(); i < path_.path_segments.end();
                i++) {
            if ((*i)->path_segment_type == As4PathSpec::PathSegment::AS_SET) {
                count++;
            } else {
                count += (*i)->path_segment.size();
            }
        }
        return count;
    }

    int CompareTo(const As4Path &rhs) const {
        const std::vector<As4PathSpec::PathSegment *> &lps = path_.path_segments;
        const std::vector<As4PathSpec::PathSegment *> &rps =
                rhs.path_.path_segments;

        KEY_COMPARE(lps.size(), rps.size());

        std::vector<As4PathSpec::PathSegment *>::const_iterator i, j;
        for (i = lps.begin(), j = rps.begin(); i < lps.end(); i++, j++) {
            int ret = (*i)->CompareTo(**j);
            if (ret != 0) return ret;
        }
        return 0;
    }
    const As4PathSpec &path() const { return path_; }
    bool empty() const { return path_.path_segments.empty(); }
    as4_t neighbor_as() const { return path_.AsLeftMost(); }

    friend std::size_t hash_value(As4Path const &as_path) {
        size_t hash = 0;
        boost::hash_combine(hash, as_path.path().ToString());
        return hash;
    }

private:
    friend int intrusive_ptr_add_ref(const As4Path *cpath);
    friend int intrusive_ptr_del_ref(const As4Path *cpath);
    friend void intrusive_ptr_release(const As4Path *cpath);

    mutable tbb::atomic<int> refcount_;
    As4PathDB *aspath_db_;
    As4PathSpec path_;
};

inline int intrusive_ptr_add_ref(const As4Path *cpath) {
    return cpath->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const As4Path *cpath) {
    return cpath->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const As4Path *cpath) {
    int prev = cpath->refcount_.fetch_and_decrement();
    if (prev == 1) {
        As4Path *path = const_cast<As4Path *>(cpath);
        path->Remove();
        assert(path->refcount_ == 0);
        delete path;
    }
}

typedef boost::intrusive_ptr<const As4Path> As4PathPtr;

struct As4PathCompare {
    bool operator()(const As4Path *lhs, const As4Path *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class As4PathDB : public BgpPathAttributeDB<As4Path, As4PathPtr, As4PathSpec,
                                           As4PathCompare, As4PathDB> {
public:
    explicit As4PathDB(BgpServer *server);

private:
};

typedef boost::intrusive_ptr<const As4Path> As4PathPtr;

#endif  // SRC_BGP_BGP_As4Path_H_
