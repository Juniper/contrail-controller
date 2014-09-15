/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_bgp_attr_base_h
#define ctrlplane_bgp_attr_base_h

#include <boost/functional/hash.hpp>
#include <boost/scoped_array.hpp>
#include <set>
#include <string>
#include <tbb/mutex.h>
#include <vector>
#include "base/parse_object.h"
#include "base/task.h"

class BgpAttr;

struct BgpAttribute : public ParseObject {
    enum Flag {
        Optional = 1 << 7,
        Transitive = 1 << 6,
        Partial = 1 << 5,
        ExtendedLength = 1 << 4
    };
    enum Code {
        Reserved = 0,
        Origin = 1,
        AsPath = 2,
        NextHop = 3,
        MultiExitDisc = 4,
        LocalPref = 5,
        AtomicAggregate = 6,
        Aggregator = 7,
        Communities = 8,
        OriginatorId = 9,
        ClusterList = 10,
        MPReachNlri = 14,
        MPUnreachNlri = 15,
        ExtendedCommunities = 16,
        PmsiTunnel = 22,
        McastEdgeDiscovery = 241,
        McastEdgeForwarding = 242,
    };
    enum Subcode {
        OList = 1,
        LabelBlock = 2,
        SourceRd = 3,
        Esi = 4,
        Params = 5
    };

    BgpAttribute() : code(0), subcode(0), flags(0) { }
    BgpAttribute(uint8_t code, uint8_t flags)
        : code(code), subcode(0), flags(flags) { }
    BgpAttribute(uint8_t code, uint8_t subcode, uint8_t flags)
        : code(code), subcode(subcode), flags(flags) { }
    static const uint8_t FLAG_MASK = Optional|Transitive;
    uint8_t code;
    uint8_t subcode;
    uint8_t flags;
    virtual std::string ToString() const;
    virtual int CompareTo(const BgpAttribute &rhs) const;
    virtual void ToCanonical(BgpAttr *attr) { }
};

//
// Canonical structure used to exchange a single NLRI prefix between the
// common parser and the address family specific BGP code.
//
// ReadLabel and WriteLabel need a label offset because the label is at
// different locations in different NLRI.
//
// The prefix length is in bits.
// The type is relevant only for NLRI that have multiple route types e.g.
// erm-vpn and e-vpn.
//
struct BgpProtoPrefix : public ParseObject {
    static const size_t kLabelSize;

    BgpProtoPrefix();

    uint32_t ReadLabel(size_t label_offset) const;
    void WriteLabel(size_t label_offset, uint32_t label);

    std::vector<uint8_t> prefix;
    int prefixlen;
    uint8_t type;
};

//
// Base class to manage BGP Path Attributes database. This class provides
// thread safe access to the data base.
//
// Lock contention can be tuned by varying the hash table size passed to the
// constructor.
//
// Attribute contents must be hashable via hash_value() and hashed using
// boost::hash_combine() to partition the attribute database.
//
template <class Type, class TypePtr, class TypeSpec, typename TypeCompare,
          class TypeDB>
class BgpPathAttributeDB {
public:
    BgpPathAttributeDB(int hash_size = GetHashSize()) :
            hash_size_(hash_size), set_(new Set[hash_size]),
            mutex_(new tbb::mutex[hash_size]) {
    }

    size_t Size() {
        size_t size = 0;

        for (size_t i = 0; i < hash_size_; i++) {
            tbb::mutex::scoped_lock lock(mutex_[i]);
            size += set_[i].size();
        }
        return size;
    }

    void Delete(Type *attr) {
        size_t hash = HashCompute(attr);

        tbb::mutex::scoped_lock lock(mutex_[hash]);
        set_[hash].erase(attr);
    }

    // Locate passed in attribute in the data base based on the attr ptr.
    TypePtr Locate(Type *attr) {
        return LocateInternal(attr);
    }

    // Locate passed in attribute in the data base, based on the attr spec.
    TypePtr Locate(const TypeSpec &spec) {
        Type *attr = new Type(static_cast<TypeDB *>(this), spec);
        return LocateInternal(attr);
    }

private:
    const size_t HashCompute(Type *attr) const {
        if (hash_size_ <= 1) return 0;

        size_t hash = 0;
        boost::hash_combine(hash, *attr);
        return hash % hash_size_;
    }

    static size_t GetHashSize() {
        char *str = getenv("BGP_PATH_ATTRIBUTE_DB_HASH_SIZE");

        // Use just one bucket for now.
        if (!str) return 1;
        return strtoul(str, NULL, 0);
    }

    // This template safely retrieves an attribute entry from its data base.
    // If the entry is not found, it is inserted into the database.
    //
    // If the entry is already present, then passed in entry is freed and
    // existing entry is returned.
    TypePtr LocateInternal(Type *attr) {

        // Hash attribute contents to to avoid potential mutex contention.
        size_t hash = HashCompute(attr);
        while (true) {

            // Grab mutex to keep db access thread safe.
            tbb::mutex::scoped_lock lock(mutex_[hash]);
            std::pair<typename Set::iterator, bool> ret;

            // Try to insert the passed entry into the database.
            ret = set_[hash].insert(attr);

            // Take a reference to prevent this entry from getting deleted.
            // Counter is automatically incremented, hence we get thread safety
            // here.
            int prev = intrusive_ptr_add_ref(*ret.first);

            // Check if passed in entry did get into the data base.
            if (ret.second) {

                // Take intrusive pointer, thereby incrementing the refcount.
                TypePtr ptr = TypePtr(*ret.first);

                // Release redundant refcount taken above to protect this entry
                // from getting deleted, as we have now bumped up refcount above
                intrusive_ptr_del_ref(*ret.first);
                return ptr;
            }

            // Make sure that this entry, though in the database is not
            // undergoing deletion. This can happen because attribute intrusive
            // pointer is released without taking the mutex.
            //
            // If the previous refcount is 0, it implies that this entry is
            // about to get deleted (after we release the mutex). In such
            // cases, we retry inserting the passed attribute pointer into the
            // data base.
            if (prev > 0) {

                // Free passed in attribute, as it is already in the database.
                delete attr;

                // Take intrusive pointer, thereby incrementing the refcount.
                TypePtr ptr = TypePtr(*ret.first);

                // Release redundant refcount taken above to protect this entry
                // from getting deleted, as we have now bumped up refcount above
                intrusive_ptr_del_ref(*ret.first);
                return ptr;
            }

            // Decrement the counter bumped up above as we can't use this entry
            // which is above to be deleted. Instead, retry inserting the passed
            // entry again, into the database.
            intrusive_ptr_del_ref(*ret.first);
        }

        assert(false);
        return NULL;
    }

    typedef std::set<Type *, TypeCompare> Set;
    size_t hash_size_;
    boost::scoped_array<Set> set_;
    boost::scoped_array<tbb::mutex> mutex_;
};

#endif
