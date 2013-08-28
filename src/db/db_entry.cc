/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <tbb/mutex.h>
#include "base/util.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include "db/db_entry.h"
#include "db/db_table_partition.h"

using namespace std;

void DBEntryBase::SetState(DBTableBase *tbl_base, ListenerId listener,
                           DBState *state) {
    DBTablePartBase *tpart = tbl_base->GetTablePartition(this);
    tbb::mutex::scoped_lock lock(tpart->dbstate_mutex());
    pair<StateMap::iterator, bool> res = state_.insert(
        make_pair(listener, state));
    if (!res.second) {
        res.first->second = state;
    } else {
        assert(!IsDeleted());
    }
}

DBState *DBEntryBase::GetState(DBTableBase *tbl_base, ListenerId listener) {
    DBTablePartBase *tpart = tbl_base->GetTablePartition(this);
    tbb::mutex::scoped_lock lock(tpart->dbstate_mutex());
    StateMap::iterator loc = state_.find(listener);
    if (loc != state_.end()) {
        return loc->second;
    }
    return NULL;
}

const DBState *DBEntryBase::GetState(const DBTableBase *tbl_base,
                                     ListenerId listener) const {
    DBTableBase *table = const_cast<DBTableBase *>(tbl_base);
    DBTablePartBase *tpart = table->GetTablePartition(this);
    tbb::mutex::scoped_lock lock(tpart->dbstate_mutex());
    StateMap::const_iterator loc = state_.find(listener);
    if (loc != state_.end()) {
        return loc->second;
    }
    return NULL;
}

void DBEntryBase::ClearState(DBTableBase *tbl_base, ListenerId listener) {
    DBTablePartBase *tpart = tbl_base->GetTablePartition(this);
    tbb::mutex::scoped_lock lock(tpart->dbstate_mutex());
    state_.erase(listener);
    if (state_.empty() && IsDeleted() && !is_onlist()) {
        assert(!IsOnRemoveQ());
        tbl_base->EnqueueRemove(this);
    }
}

bool DBEntryBase::is_state_empty(DBTablePartBase *tpart) {
    tbb::mutex::scoped_lock lock(tpart->dbstate_mutex());
    return state_.empty(); 
}

void DBEntryBase::set_last_change_at_to_now() {
    last_change_at_ = UTCTimestampUsec();
}

void DBEntryBase::set_last_change_at(uint64_t time) {
    last_change_at_ = time;
}

const std::string DBEntryBase::last_change_at_str() const {
    return boost::lexical_cast<std::string>
                (UTCUsecToPTime(last_change_at_));
}
