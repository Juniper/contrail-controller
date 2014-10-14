/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_db_entry_h
#define ctrlplane_db_entry_h

#include <map>

#include <tbb/atomic.h>

#include "db/db_table.h"

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

struct DBState {
    virtual ~DBState() { }
};

// Generic database entry
class DBEntryBase {
public:
    typedef DBTableBase::ListenerId ListenerId;
    typedef std::auto_ptr<DBRequestKey> KeyPtr;

    DBEntryBase() : tpart_(NULL), flags(0), last_change_at_(UTCTimestampUsec()) {
        onremoveq_ = false;
    }
    virtual ~DBEntryBase() { }
    virtual std::string ToString() const = 0;
    virtual KeyPtr GetDBRequestKey() const = 0;
    virtual bool IsMoreSpecific(const std::string &match) const {
        return false;
    }

    void SetState(DBTableBase *tbl_base, ListenerId listener, DBState *state);
    void ClearState(DBTableBase *tbl_base, ListenerId listener);
    DBState *GetState(DBTableBase *tbl_base, ListenerId listener);
    const DBState *GetState(const DBTableBase *tbl_base,
                            ListenerId listener) const;
    bool is_state_empty(DBTablePartBase *tpart);

    void MarkDelete() { flags |= DeleteMarked; }
    void ClearDelete() { flags &= ~DeleteMarked; }
    bool IsDeleted() const { return (flags&DeleteMarked); }

    void set_onlist() { flags |= Onlist; }
    void clear_onlist() { flags &= ~Onlist; }
    bool is_onlist() { return (flags & Onlist); }

    void SetOnRemoveQ() {
        onremoveq_.fetch_and_store(true);
    }
    bool IsOnRemoveQ() { return (onremoveq_); }
    void ClearOnRemoveQ() {
        onremoveq_.fetch_and_store(false);
    }

    //member hook in change list
    boost::intrusive::list_member_hook<> chg_list_;

    void set_last_change_at(uint64_t time);
    void set_last_change_at_to_now();
    const uint64_t last_change_at() const { return last_change_at_; }
    const std::string last_change_at_str() const;
    DBTablePartBase *get_table_partition() const;
    void set_table_partition(DBTablePartBase *tpart);
    DBTableBase *get_table() const;

private:
    enum DbEntryFlags {
        Onlist       = 1 << 0,
        DeleteMarked = 1 << 1,
    };
    typedef std::map<ListenerId, DBState *> StateMap;
    DBTablePartBase *tpart_;
    StateMap state_;
    uint8_t flags;
    tbb::atomic<bool> onremoveq_;
    uint64_t last_change_at_; // time at which entry was last 'changed'
    DISALLOW_COPY_AND_ASSIGN(DBEntryBase);
};

// An implementation of DBEntryBase that uses boost::set as data-store
// Most of the DB Table implementations should derive from here instead of
// DBEntryBase directly.
// Derive directly from DBEntryBase only if there is a strong reason to do so
class DBEntry : public DBEntryBase {
public:
    DBEntry() { };
    virtual ~DBEntry() { };

    // Set key fields in the DBEntry
    virtual void SetKey(const DBRequestKey *key) = 0;

    // Comparator used in Tree management 
    virtual bool IsLess(const DBEntry &rhs) const = 0;

    bool operator<(const DBEntry &rhs) const {
        return IsLess(rhs);
    }

private:
    friend class DBTablePartition;
    boost::intrusive::set_member_hook<> node_;
    DISALLOW_COPY_AND_ASSIGN(DBEntry);
};

#endif
