/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef BASE__MAP_DIFFERENCE_H__
#define BASE__MAP_DIFFERENCE_H__

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

#endif  // BASE__MAP_DIFFERENCE_H__
