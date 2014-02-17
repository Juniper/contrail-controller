/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DB_IFMAP_UPDATE_H__
#define __DB_IFMAP_UPDATE_H__

#include <boost/crc.hpp>      // for boost::crc_32_type
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>

#include "base/bitset.h"
#include "base/dependency.h"
#include "db/db_entry.h"
#include "ifmap/ifmap_link.h"

struct IFMapObjectPtr {
    enum ObjectType {
        NIL,
        NODE,
        LINK
    };

    IFMapObjectPtr();
    explicit IFMapObjectPtr(IFMapNode *node);
    explicit IFMapObjectPtr(IFMapLink *link);

    void set(IFMapNode *node) {
        type = NODE;
        u.node = node;
    }
    void set(IFMapLink *link) {
        type = LINK;
        u.link = link;
    }
    bool IsNode() const { return ((type == NODE) ? true : false); }
    bool IsLink() const { return ((type == LINK) ? true : false); }
    ObjectType type;
    union {
        void *ptr;
        IFMapNode *node;
        IFMapLink *link;
    } u;
};

struct IFMapListEntry {
    enum EntryType {
        UPDATE,         // add or change operation
        DELETE,         // delete
        MARKER
    };
    IFMapListEntry(EntryType type) : type(type) { }
    boost::intrusive::list_member_hook<> node;
    EntryType type;
    bool IsMarker() const { return ((type == MARKER) ? true : false); }
    bool IsUpdate() const { return ((type == UPDATE) ? true : false); }
    bool IsDelete() const { return ((type == DELETE) ? true : false); }
    std::string TypeToString() {
        if (IsMarker()) {
            return "Marker";
        } else if (IsUpdate()) {
            return "Update";
        } else if (IsDelete()) {
            return "Delete";
        } else {
            return "Unknown";
        }
    }
};

class IFMapUpdate : public IFMapListEntry {
public:
    IFMapUpdate(IFMapNode *node, bool positive);
    IFMapUpdate(IFMapLink *link, bool positive);

    void AdvertiseReset(const BitSet &set);
    void AdvertiseOr(const BitSet &set);
    void SetAdvertise(const BitSet &set);
    const BitSet &advertise() const { return advertise_; }

    const IFMapObjectPtr &data() const { return data_; }
    std::string ConfigName();
    std::string ToString();
    bool IsNode() const { return data_.IsNode(); }
    bool IsLink() const { return data_.IsLink(); }

private:
    friend class IFMapState;
    boost::intrusive::slist_member_hook<> node_;
    IFMapObjectPtr data_;
    BitSet advertise_;
};

struct IFMapMarker : public IFMapListEntry {
    IFMapMarker();
    BitSet mask;
};

// State associated with each DB entry.
class IFMapState : public DBState {
public:
    typedef boost::crc_32_type::value_type crc32type;
    typedef boost::intrusive::member_hook<
        IFMapUpdate, boost::intrusive::slist_member_hook<>, &IFMapUpdate::node_
    > MemberHook;
    typedef boost::intrusive::slist<IFMapUpdate, MemberHook> UpdateList;

    IFMapState();
    virtual ~IFMapState();

    const BitSet &interest() const { return interest_; }
    const BitSet &advertised() const { return advertised_; }

    const UpdateList &update_list() const { return update_list_; }
    IFMapUpdate *GetUpdate(IFMapListEntry::EntryType type);
    void Insert(IFMapUpdate *update);
    void Remove(IFMapUpdate *update);

    void InterestOr(const BitSet &bset) { interest_ |= bset; }
    void SetInterest(const BitSet &bset) { interest_ = bset; }
    void InterestReset(const BitSet &set) { interest_.Reset(set); }
    
    void AdvertisedOr(const BitSet &set) { advertised_ |= set; }
    void AdvertisedReset(const BitSet &set) { advertised_.Reset(set); }

    template <typename Disposer>
    void ClearAndDispose(Disposer disposer) {
        update_list_.clear_and_dispose(disposer);
    }

    virtual void SetValid() { sig_ = 0; }
    virtual void ClearValid() { sig_ = kInvalidSig; }
    virtual bool IsValid() const { return sig_ != kInvalidSig; }
    virtual bool IsInvalid() const { return sig_ == kInvalidSig; }
    const crc32type &crc() const { return crc_; }
    void SetCrc(crc32type &crc) { crc_ = crc; }

protected:
    static const uint32_t kInvalidSig = -1;
    uint32_t sig_;

private:

    // The set of clients known to be interested in this update.
    BitSet interest_;
    // The set of clients to which this update has been advertised.
    BitSet advertised_;

    UpdateList update_list_;

    crc32type crc_;
};

class IFMapNodeState : public IFMapState {
public:
    typedef DependencyList<IFMapLink, IFMapNodeState>::iterator iterator;
    typedef DependencyList<IFMapLink, IFMapNodeState>::const_iterator
            const_iterator;
    IFMapNodeState();

    void SetValid() { IFMapState::SetValid(); }
    void SetValid(const IFMapNode *node) { sig_ = 0; }
    bool HasDependents() const;

    iterator begin() { return dependents_.begin(); }
    iterator end() { return dependents_.end(); }

    const_iterator begin() const { return dependents_.begin(); }
    const_iterator end() const { return dependents_.end(); }

    const BitSet &nmask() const { return nmask_; }
    void nmask_clear() { nmask_.clear(); }
    void nmask_set(int bit) { nmask_.set(bit); }

private:
    DEPENDENCY_LIST(IFMapLink, IFMapNodeState, dependents_);
    BitSet nmask_;          // new bitmask computed by graph traversal
};

class IFMapLinkState : public IFMapState {
public:
    explicit IFMapLinkState(IFMapLink *link);
    void SetDependency(IFMapNodeState *first, IFMapNodeState *second);
    void RemoveDependency();
    bool HasDependency() const;

    void SetValid() { IFMapState::SetValid(); }
    void SetValid(const IFMapLink *link) { sig_ = 0; }

    IFMapNodeState *left() { return left_.get(); }
    IFMapNodeState *right() { return right_.get(); }

private:
    DependencyRef<IFMapLink, IFMapNodeState> left_;
    DependencyRef<IFMapLink, IFMapNodeState> right_;
};

#endif
