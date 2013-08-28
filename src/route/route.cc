/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "route/route.h"

Route::Route() {
}

Route::~Route() {
}

// Selected path
const Path *Route::front() const {
    PathList::const_iterator it = path_.begin();
    if (it == path_.end())
        return NULL;
    return it.operator->();
}

// Insert a path
void Route::insert(const Path *ipath) {
    Path *path = const_cast<Path *> (ipath);

    path->set_time_stamp_usecs(UTCTimestampUsec());
    path_.push_back(*path);
}

// Remove a path
void Route::remove(const Path *ipath) {
    Path *path = const_cast<Path *> (ipath);

    path->set_time_stamp_usecs(UTCTimestampUsec());
    PathList::const_iterator eraseIt = path_.iterator_to(*path);
    path_.erase(eraseIt);
}

void Route::Sort(Compare compare, const Path *prev_front) {
    path_.sort(compare);

    // If the best path changes, update route's time stamp.
    if (prev_front != front()) {
        set_last_change_at_to_now();
    }
}
