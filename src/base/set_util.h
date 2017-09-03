/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef BASE_SET_UTIL_H
#define BASE_SET_UTIL_H

//
// set_synchronize
//
// Given two sets synchronize the 1st (current) set with the 2nd (future)
// one by invoking add or delete functors for values that are:
//   add - present in the 2nd set but not in the 1st;
//   delete - present in the 1st set but not in the 2nd;
//
// The add/delete functors are responsible for adding/deleting appropriate
// elements to/from the 1st set.
//
// Returns true if any set is modified, false if they are identical.
//
template <typename SetType, typename AddFunctor, typename DelFunctor>
bool set_synchronize(SetType *set1, const SetType *set2,
                     AddFunctor add_fn, DelFunctor del_fn) {
    typename SetType::iterator it1 = set1->begin(), next1 = set1->begin();
    typename SetType::const_iterator it2 = set2->begin();
    bool modified = false;
    while (it1 != set1->end() && it2 != set2->end()) {
        if (*it1 < *it2) {
            ++next1;
            modified = true;
            del_fn(it1);
            it1 = next1;
        } else if (*it1 > *it2) {
            modified = true;
            add_fn(it2);
            ++it2;
        } else {
            ++it1;
            ++it2;
        }
        next1 = it1;
    }
    for (next1 = it1; it1 != set1->end(); it1 = next1) {
        ++next1;
        modified = true;
        del_fn(it1);
    }
    for (; it2 != set2->end(); ++it2) {
        modified = true;
        add_fn(it2);
    }
    return modified;
}

#endif  // BASE_SET_UTIL_H
