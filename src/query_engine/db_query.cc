/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "query.h"

query_status_t DbQueryUnit::process_query()
{
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    uint32_t t2_start = m_query->from_time >> g_viz_constants.RowTimeInBits;
    uint32_t t2_end = m_query->end_time >> g_viz_constants.RowTimeInBits;

    QE_TRACE(DEBUG,  " Database query for " << 
            (t2_end - t2_start + 1) << " rows");

    QE_TRACE(DEBUG,  " Database query for T2_start:"
            << t2_start
            << " T2_end:" << t2_end
            << " cf:" << cfname
            << " column_start size:" << cr.start_.size()
            << " column_end size:" << cr.finish_.size());

    GenDb::DbDataValue timestamp_end = (uint32_t)(0xffffffff);
    cr.finish_.push_back(timestamp_end);

    for (uint32_t t2 = t2_start; t2 <= t2_end; t2++)
    {
        GenDb::ColList result;
        GenDb::DbDataValueVec rowkey;

        if (t_only_row)
        {
            rowkey.push_back(t2);
        } else {
            rowkey.push_back(t2);
            rowkey.push_back(row_key_suffix);
        }
        
        if (m_query->dbif->Db_GetRangeSlices(result, cfname, cr, rowkey))
        {
            std::vector<GenDb::NewCol>::iterator i;

            QE_TRACE(DEBUG, "For T2:" << t2 <<
                " Database returned " << result.columns_.size() << " cols");

            for (i = result.columns_.begin(); i != result.columns_.end(); i++)
            {
                {
                    query_result_unit_t result_unit;

                    int ts_at = i->name.size() - 1;
                    assert(ts_at >= 0);
                    uint32_t t1;
                    try {
                        t1 = boost::get<uint32_t>(i->name.at(ts_at));
                    } catch (boost::bad_get& ex) {
                        assert(0);
                    }
                    result_unit.timestamp = TIMESTAMP_FROM_T2T1(t2, t1);

                    if 
                    ((result_unit.timestamp < m_query->from_time) ||
                     (result_unit.timestamp > m_query->end_time))
                    {
                        //QE_TRACE(DEBUG, "Discarding timestamp "
                        //        << result_unit.timestamp);
                        // got a result outside of the time range
                        continue;
                    }

                    // Add to result vector
                    result_unit.info = i->value;
                    query_result.push_back(result_unit);
                }
            }
        } // TBD handle database query errors
    }

    // Have the result ready and processing is done
    // sort the result before returning
    std::sort(query_result.begin(), query_result.end());

    QE_TRACE(DEBUG,  " Database query completed with "
            << query_result.size() << " rows");
    for (unsigned int i = 0; i < query_result.size(); i++)
        QE_TRACE(DEBUG, (i+1) << " : " << query_result[i]);

    status_details = 0;
    parent_query->subquery_processed(this);
    return QUERY_SUCCESS;
}
