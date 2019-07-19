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
class As4PathDB;
class AsPath4ByteDB;
class BgpServer;

struct AsPathSpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Transitive;
    static const as2_t kMinPrivateAs = 64512;
    static const as2_t kMaxPrivateAs = 65534;

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
        std::vector<as2_t> path_segment;
    };

    as2_t AsLeftMost() const;
    bool AsLeftMostMatch(as2_t as) const;
    as2_t AsLeftMostPublic() const;
    bool AsPathLoop(as2_t as, uint8_t max_loop_count = 0) const;
    static bool AsIsPrivate(as2_t as) {
        return as >= kMinPrivateAs && as <= kMaxPrivateAs;
    }

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual size_t EncodeLength() const;
    virtual std::string ToString() const;
    AsPathSpec *Add(as2_t asn) const;
    AsPathSpec *Add(const std::vector<as2_t> &asn_list) const;
    AsPathSpec *Add(const std::vector<as_t> &asn_list) const;
    AsPathSpec *Replace(as2_t old_asn, as2_t asn) const;
    AsPathSpec *RemovePrivate(bool all, as2_t asn, as2_t peer_as) const;

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
    as2_t neighbor_as() const { return path_.AsLeftMost(); }

    friend std::size_t hash_value(const AsPath &as_path) {
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

struct AsPath4ByteSpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Transitive;
    static const as_t kMinPrivateAs = 64512;
    static const as_t kMaxPrivateAs = 65534;
    static const as_t kMinPrivateAs4 = 4200000000;
    static const as_t kMaxPrivateAs4 = 4294967294;

    AsPath4ByteSpec() : BgpAttribute(BgpAttribute::AsPath, kFlags) {}
    explicit AsPath4ByteSpec(const BgpAttribute &rhs) : BgpAttribute(rhs) {}
    explicit AsPath4ByteSpec(const AsPath4ByteSpec &rhs) :
            BgpAttribute(BgpAttribute::AsPath, kFlags) {
        for (size_t i = 0; i < rhs.path_segments.size(); i++) {
            PathSegment *ps = new PathSegment;
            *ps = *rhs.path_segments[i];
            path_segments.push_back(ps);
        }
    }
    ~AsPath4ByteSpec() {
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
    as_t AsLeftMostPublic() const;
    bool AsPathLoop(as_t as, uint8_t max_loop_count = 0) const;
    static bool AsIsPrivate(as_t as) {
        return ((as >= kMinPrivateAs && as <= kMaxPrivateAs) ||
            (as >= kMinPrivateAs4 && as <= kMaxPrivateAs4));
    }

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual size_t EncodeLength() const;
    virtual std::string ToString() const;
    AsPath4ByteSpec *Add(as_t asn) const;
    AsPath4ByteSpec *Add(const std::vector<as_t> &asn_list) const;
    AsPath4ByteSpec *Replace(as_t old_asn, as_t asn) const;
    AsPath4ByteSpec *RemovePrivate(bool all, as_t asn, as_t peer_as) const;

    std::vector<PathSegment *> path_segments;
};

class AsPath4Byte {
public:
    explicit AsPath4Byte(AsPath4ByteDB *aspath_db) : aspath_db_(aspath_db) {
        refcount_ = 0;
    }
    explicit AsPath4Byte(AsPath4ByteDB *aspath_db, const AsPath4ByteSpec &spec)
        : aspath_db_(aspath_db), path_(spec) {
        refcount_ = 0;
        for (size_t i = 0; i < path_.path_segments.size(); i++) {
            AsPath4ByteSpec::PathSegment *ps = path_.path_segments[i];
            if (ps->path_segment_type == AsPath4ByteSpec::PathSegment::AS_SET) {
                std::sort(ps->path_segment.begin(), ps->path_segment.end());
            }
        }
    }
    virtual ~AsPath4Byte() { }
    virtual void Remove();
    int AsCount() const {
        int count = 0;
        std::vector<AsPath4ByteSpec::PathSegment *>::const_iterator i;
        for (i = path_.path_segments.begin(); i < path_.path_segments.end();
                i++) {
            if ((*i)->path_segment_type ==
                             AsPath4ByteSpec::PathSegment::AS_SET) {
                count++;
            } else {
                count += (*i)->path_segment.size();
            }
        }
        return count;
    }

    int CompareTo(const AsPath4Byte &rhs) const {
        const std::vector<AsPath4ByteSpec::PathSegment *> &lps =
                path_.path_segments;
        const std::vector<AsPath4ByteSpec::PathSegment *> &rps =
                rhs.path_.path_segments;

        KEY_COMPARE(lps.size(), rps.size());

        std::vector<AsPath4ByteSpec::PathSegment *>::const_iterator i, j;
        for (i = lps.begin(), j = rps.begin(); i < lps.end(); i++, j++) {
            int ret = (*i)->CompareTo(**j);
            if (ret != 0) return ret;
        }
        return 0;
    }
    const AsPath4ByteSpec &path() const { return path_; }
    bool empty() const { return path_.path_segments.empty(); }
    as_t neighbor_as() const { return path_.AsLeftMost(); }

    friend std::size_t hash_value(const AsPath4Byte &as_path) {
        size_t hash = 0;
        boost::hash_combine(hash, as_path.path().ToString());
        return hash;
    }

private:
    friend int intrusive_ptr_add_ref(const AsPath4Byte *cpath);
    friend int intrusive_ptr_del_ref(const AsPath4Byte *cpath);
    friend void intrusive_ptr_release(const AsPath4Byte *cpath);

    mutable tbb::atomic<int> refcount_;
    AsPath4ByteDB *aspath_db_;
    AsPath4ByteSpec path_;
};

inline int intrusive_ptr_add_ref(const AsPath4Byte *cpath) {
    return cpath->refcount_.fetch_and_increment();
}

inline int intrusive_ptr_del_ref(const AsPath4Byte *cpath) {
    return cpath->refcount_.fetch_and_decrement();
}

inline void intrusive_ptr_release(const AsPath4Byte *cpath) {
    int prev = cpath->refcount_.fetch_and_decrement();
    if (prev == 1) {
        AsPath4Byte *path = const_cast<AsPath4Byte *>(cpath);
        path->Remove();
        assert(path->refcount_ == 0);
        delete path;
    }
}

typedef boost::intrusive_ptr<const AsPath4Byte> AsPath4BytePtr;

struct AsPath4ByteCompare {
    bool operator()(const AsPath4Byte *lhs, const AsPath4Byte *rhs) {
        return lhs->CompareTo(*rhs) < 0;
    }
};

class AsPath4ByteDB : public BgpPathAttributeDB<AsPath4Byte, AsPath4BytePtr,
                         AsPath4ByteSpec, AsPath4ByteCompare, AsPath4ByteDB> {
public:
    explicit AsPath4ByteDB(BgpServer *server);

private:
};

typedef boost::intrusive_ptr<const AsPath4Byte> AsPath4BytePtr;

struct As4PathSpec : public BgpAttribute {
    static const int kSize = -1;
    static const uint8_t kFlags = Transitive|Optional;
    static const as_t kMinPrivateAs = 64512;
    static const as_t kMaxPrivateAs = 65534;
    static const as_t kMinPrivateAs4 = 4200000000;
    static const as_t kMaxPrivateAs4 = 4294967294;

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
        std::vector<as_t> path_segment;
    };

    as_t AsLeftMost() const;
    bool AsLeftMostMatch(as_t as) const;
    as_t AsLeftMostPublic() const;
    bool AsPathLoop(as_t as, uint8_t max_loop_count = 0) const;
    static bool AsIsPrivate(as_t as) {
        return ((as >= kMinPrivateAs && as <= kMaxPrivateAs) ||
            (as >= kMinPrivateAs4 && as <= kMaxPrivateAs4));
    }

    virtual int CompareTo(const BgpAttribute &rhs_attr) const;
    virtual void ToCanonical(BgpAttr *attr);
    virtual size_t EncodeLength() const;
    virtual std::string ToString() const;
    As4PathSpec *Add(as_t asn) const;
    As4PathSpec *Add(const std::vector<as_t> &asn_list) const;
    As4PathSpec *Replace(as_t old_asn, as_t asn) const;
    As4PathSpec *RemovePrivate(bool all, as_t asn, as_t peer_as) const;

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
    as_t neighbor_as() const { return path_.AsLeftMost(); }

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
#endif  // SRC_BGP_BGP_ASPATH_H_
