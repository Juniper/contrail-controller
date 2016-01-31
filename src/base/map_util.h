/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef BASE_MAP_UTIL_H
#define BASE_MAP_UTIL_H

/*
 * map_difference
 *
 * Given two sorted maps, invoke add, delete or equal functors for values
 * that are:
 *  add - present in the second map but not in the first;
 *  delete - present in the first map but not in the second;
 *  equal - present in both maps;
 */
template <typename ForwardIterator,
    typename AddFunctor,
    typename DelFunctor,
    typename EqFunctor>
void map_difference(ForwardIterator __first1, ForwardIterator __last1,
                    ForwardIterator __first2, ForwardIterator __last2,
                    AddFunctor __add_fn, DelFunctor __del_fn,
                    EqFunctor __eq_fn) {
    while (__first1 != __last1 && __first2 != __last2) {
        if (__first1->first < __first2->first) {
            __del_fn(__first1);
            ++__first1;
        } else if (__first1->first > __first2->first) {
            __add_fn(__first2);
            ++__first2;
        } else {
            __eq_fn(__first1, __first2);
            ++__first1;
            ++__first2;
        }
    }
    for (; __first1 != __last1;  ++__first1) {
        __del_fn(__first1);
    }
    for (; __first2 != __last2;  ++__first2) {
        __add_fn(__first2);
    }
}

//
// map_difference
//
// Given a map and the begin/end iterators for a sorted std container and a
// compare functor, invoke add, delete or equal functors for values that are:
//  add - present in the container but not in the map;
//  delete - present in the map but not in the container;
//  equal - present in both the container and the map;
//
template <typename MapType,
    typename ForwardIterator,
    typename CompFunctor,
    typename AddFunctor,
    typename DelFunctor,
    typename EqFunctor>
void map_difference(MapType *map, ForwardIterator first, ForwardIterator last,
                    CompFunctor comp_fn, AddFunctor add_fn,
                    DelFunctor del_fn, EqFunctor eq_fn) {
    typename MapType::iterator it1 = map->begin(), next1 = map->begin();
    ForwardIterator it2 = first;
    while (it1 != map->end() && it2 != last) {
        int result = comp_fn(it1, it2);
        if (result < 0) {
            ++next1;
            del_fn(it1);
            it1 = next1;
        } else if (result > 0) {
            add_fn(it2);
            ++it2;
        } else {
            eq_fn(it1, it2);
            ++it1;
            ++it2;
        }
        next1 = it1;
    }
    for (next1 = it1; it1 != map->end(); it1 = next1) {
        ++next1;
        del_fn(it1);
    }
    for (; it2 != last; ++it2) {
        add_fn(it2);
    }
}

//
// map_synchronize
//
// Given two maps synchronize the 1st (current) map with the 2nd (future)
// one by invoking add or delete functors for values that are:
//   add - present in the 2nd map but not in the 1st;
//   delete - present in the 1st map but not in the 2nd;
//
// The add/delete functors are responsible for adding/deleting appropriate
// elements to/from the 1st map.
//
template <typename MapType, typename AddFunctor, typename DelFunctor>
void map_synchronize(MapType *map1, const MapType *map2,
                     AddFunctor add_fn, DelFunctor del_fn) {
    typename MapType::iterator it1 = map1->begin(), next1 = map1->begin();
    typename MapType::const_iterator it2 = map2->begin();
    while (it1 != map1->end() && it2 != map2->end()) {
        if (it1->first < it2->first) {
            ++next1;
            del_fn(it1);
            it1 = next1;
        } else if (it1->first > it2->first) {
            add_fn(it2);
            ++it2;
        } else {
            ++it1;
            ++it2;
        }
        next1 = it1;
    }
    for (next1 = it1; it1 != map1->end(); it1 = next1) {
        ++next1;
        del_fn(it1);
    }
    for (; it2 != map2->end(); ++it2) {
        add_fn(it2);
    }
}

#endif  // BASE_MAP_UTIL_H
