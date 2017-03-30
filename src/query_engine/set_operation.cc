/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "query.h"

using std::vector;
using boost::shared_ptr;
using std::string;

// for sorting and set operations
bool query_result_unit_t::operator<(const query_result_unit_t& rhs) const
{
    if (timestamp == rhs.timestamp)
    {
        GenDb::DbDataValueVec::const_iterator it = info.begin();
        GenDb::DbDataValueVec::const_iterator jt = rhs.info.begin();
        for (; it != info.end() && jt != rhs.info.end(); it++, jt++) {
            if (*it < *jt) {
                return true;
            } else if (*jt < *it) {
                return false;
            }
        }
        return false;
    }

    return (timestamp < rhs.timestamp);
}


void
SetOperationUnit::op_and(string qi, WhereResultT& res,
        vector<WhereResultT*> inp) {
    res = *inp[0];
    for (size_t and_idx=1; and_idx<inp.size(); and_idx++) {
        WhereResultT tmp_query_result;
        QE_LOG_NOQID(INFO, qi << " INT between tables of sizes " << 
                res.size() << " and " << inp[and_idx]->size());
        set_intersection(res.begin(), res.end(),
                inp[and_idx]->begin(),
                inp[and_idx]->end(),
                std::back_inserter(tmp_query_result));
        res = tmp_query_result;    // keep the result in output var
        QE_LOG_NOQID(INFO, qi << " Resulting size of set " <<
                res.size());
        
    }
}

void
SetOperationUnit::op_or(string qi, WhereResultT& res,
        vector<WhereResultT*> inp) {
    res = *inp[0];
    for (size_t or_idx=1; or_idx<inp.size(); or_idx++) {
            // If the OR term has mulitple ORs, union is needed
            WhereResultT tmp_query_result;
            QE_LOG_NOQID(INFO, qi << " UNION between tables of sizes " << 
                    res.size() << " and " << inp[or_idx]->size());
            set_union(res.begin(), res.end(),
                    inp[or_idx]->begin(),
                    inp[or_idx]->end(),
                    std::back_inserter(tmp_query_result));
            res = tmp_query_result;    // keep the result in output var
            QE_LOG_NOQID(INFO, qi <<  " Resulting size of set " <<
                    res.size());
    }
}

