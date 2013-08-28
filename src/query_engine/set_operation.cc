/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "query.h"

// for sorting and set operations
bool query_result_unit_t::operator<(const query_result_unit_t& rhs) const
{
#if 0
    if (timestamp == rhs.timestamp)
    {
        GenDb::DbDataValueVec::iterator it = info.begin();
        GenDb::DbDataValueVec::iterator jt = rhs.info.begin();
        for (; it != info.end(), jt != rhs.info.end(); it++, jt++) {
            if (*it < *jt) {
                return true;
            } else if (*it > *jt) {
                return false;
            }
        }
        return false;
    }
#endif

    return (timestamp < rhs.timestamp);
}

void SetOperationUnit::or_operation()
{
    if (sub_queries.size() == 0)
    {
        QE_TRACE(DEBUG, "Empty where sub-clause");
        return;
    }

    // with one query no need to do any operation
    query_result = sub_queries[0]->query_result;

    for (unsigned int i = 1; i < sub_queries.size(); i++)
    {
        std::vector<query_result_unit_t> tmp_query_result;

        QE_TRACE(DEBUG, "UNION between tables of sizes " << 
                query_result.size() << " and " <<
                sub_queries[i]->query_result.size());
        set_union(query_result.begin(), query_result.end(),
                sub_queries[i]->query_result.begin(), 
                sub_queries[i]->query_result.end(),
                std::back_inserter(tmp_query_result));

        query_result = tmp_query_result;    // keep the result in output var
        QE_TRACE(DEBUG, "Resulting size of set " << query_result.size());
    }
}

void SetOperationUnit::and_operation()
{
    if (sub_queries.size() == 0)
    {
        QE_TRACE(DEBUG, "Empty where sub-clause");
        return;
    }

    // with one query no need to do any operation
    query_result = sub_queries[0]->query_result;

    for (unsigned int i = 1; i < sub_queries.size(); i++)
    {
        std::vector<query_result_unit_t> tmp_query_result;

        QE_TRACE(DEBUG, "INT between tables of sizes " << 
                query_result.size() << " and " <<
                sub_queries[i]->query_result.size());
        set_intersection(query_result.begin(), query_result.end(),
                sub_queries[i]->query_result.begin(), 
                sub_queries[i]->query_result.end(),
                std::back_inserter(tmp_query_result));

        query_result = tmp_query_result;    // keep the result in output var
        QE_TRACE(DEBUG, "Resulting size of set " << query_result.size());
    }
}


query_status_t SetOperationUnit::process_query()
{
    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return QUERY_FAILURE;
    }

    QE_TRACE(DEBUG, 
             " No of subset queries:"  << sub_queries.size());
    // invoke processing of all the sub queries
    // TBD: Handle ASYNC processing
    for (unsigned int i = 0; i < sub_queries.size(); i++)
    {
        query_status_t query_status = sub_queries[i]->process_query();

        if (query_status == QUERY_FAILURE)
        {
            status_details = sub_queries[i]->status_details;
            return QUERY_FAILURE;
        }
    }

    QE_TRACE(DEBUG, "Set operation between " << sub_queries.size()
            << " tables");

    // Do the SET operation
    switch (set_operation)
    {
        case UNION_OP: 
            QE_TRACE(DEBUG, "Now do UNION set operation");
            or_operation();
            break;

        case INTERSECTION_OP:
            QE_TRACE(DEBUG, "Now do INTERSECTION set operation");
            and_operation();
            break;

        default:
            // Dont know what to do
            if (sub_queries.size() != 0)
                query_result = sub_queries[0]->query_result;
            break;
    }

    // Have the result ready and processing is done
    status_details = 0;
    parent_query->subquery_processed(this);
    return QUERY_SUCCESS;
}

