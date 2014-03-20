/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "query.h"

query_status_t DbQueryUnit::process_query()
{
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    uint32_t t2_start = m_query->from_time() >> g_viz_constants.RowTimeInBits;
    uint32_t t2_end = m_query->end_time() >> g_viz_constants.RowTimeInBits;

    QE_TRACE(DEBUG,  " Database query for " << 
            (t2_end - t2_start + 1) << " rows");

    QE_TRACE(DEBUG,  " Database query for T2_start:"
            << t2_start
            << " T2_end:" << t2_end
            << " cf:" << cfname
            << " column_start size:" << cr.start_.size()
            << " column_end size:" << cr.finish_.size());

    if (m_query->is_object_table_query())
    {    
        GenDb::DbDataValue timestamp_start = (uint32_t)0x0;
        cr.start_.push_back(timestamp_start);
    }
    GenDb::DbDataValue timestamp_end = (uint32_t)(0xffffffff);
    cr.finish_.push_back(timestamp_end);

    std::vector<GenDb::DbDataValueVec> keys;    // vector of keys for multi-row get
    GenDb::ColListVec mget_res;   // vector of result for each row
    for (uint32_t t2 = t2_start; t2 <= t2_end; t2++)
    {
        GenDb::ColList result;
        GenDb::DbDataValueVec rowkey;

        rowkey.push_back(t2);
        if (m_query->is_flow_query() || m_query->is_object_table_query()) {
            uint8_t partition_no = 0;
            rowkey.push_back(partition_no);
        }

        if (!t_only_row)
        {
            for (GenDb::DbDataValueVec::iterator it = row_key_suffix.begin();
                    it!=row_key_suffix.end(); it++) {
                rowkey.push_back(*it);
            }
        }
        keys.push_back(rowkey);
    }

    if (!m_query->dbif->Db_GetMultiRow(mget_res, cfname, keys, &cr)) {
         QE_TRACE(DEBUG, "GetMultiRow failed:keys count:"<< keys.size() <<" :cr_s(size):"<<cr.start_.size()<<" :cr_f(size):"<<cr.finish_.size()<<" :cr_s(0):"<<cr.start_.at(0) <<" :cr_f(0):"<<cr.finish_.at(0)<<" :cr_f(1):"<<cr.finish_.at(1));
        for (size_t i = 0; i < keys.size(); i++)
            QE_TRACE(DEBUG, "GetMultiRow failed:keys:"<<i<<":"<<keys[i].at(0) << ":keys[1]:" << (keys[i].at(1)));
   
        QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
    } else {
        for (GenDb::ColListVec::iterator it = mget_res.begin();
                it != mget_res.end(); it++) {
            uint32_t t2 = boost::get<uint32_t>(it->rowkey_.at(0));
            GenDb::NewColVec::iterator i;

            QE_TRACE(DEBUG, "For T2:" << t2 <<
                " Database returned " << it->columns_.size() << " cols");

            for (i = it->columns_.begin(); i != it->columns_.end(); i++)
            {
                {
                    query_result_unit_t result_unit;
                    uint32_t t1;
                    
                    if (m_query->is_stat_table_query()) {
                        assert(i->name->size()==4);
                        assert(i->value->size()==1);                        
                        try {
                            t1 = boost::get<uint32_t>(i->name->at(2));
                        } catch (boost::bad_get& ex) {
                            assert(0);
                        }
                    } else if (m_query->is_flow_query()) {
                        int ts_at = i->name->size() - 2;
                        assert(ts_at >= 0);
                        
                        try {
                            t1 = boost::get<uint32_t>(i->name->at(ts_at));
                        } catch (boost::bad_get& ex) {
                            assert(0);
                        }
                    } else {
                        int ts_at = i->name->size() - 1;
                        assert(ts_at >= 0);
                        
                        try {
                            t1 = boost::get<uint32_t>(i->name->at(ts_at));
                        } catch (boost::bad_get& ex) {
                            assert(0);
                        }
                    }
                    result_unit.timestamp = TIMESTAMP_FROM_T2T1(t2, t1);

                    if 
                    ((result_unit.timestamp < m_query->from_time()) ||
                     (result_unit.timestamp > m_query->end_time()))
                    {
                        //QE_TRACE(DEBUG, "Discarding timestamp "
                        //        << result_unit.timestamp);
                        // got a result outside of the time range
                        continue;
                    }

                    // Add to result vector
                    if (m_query->is_stat_table_query()) {
                        std::string attribstr;
                        boost::uuids::uuid uuid;

                        try {
                            uuid = boost::get<boost::uuids::uuid>(i->name->at(3));
                        } catch (boost::bad_get& ex) {
                            QE_ASSERT(0);
                        } catch (const std::out_of_range& oor) {
                            QE_ASSERT(0);
                        }

                        try {
                            attribstr = boost::get<std::string>(i->value->at(0));
                        } catch (boost::bad_get& ex) {
                            QE_ASSERT(0);
                        } catch (const std::out_of_range& oor) {
                            QE_ASSERT(0);
                        }

                        result_unit.set_stattable_info(
                            attribstr,
                            uuid);
                    } else {
                        result_unit.info = *i->value;
                    }

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

    status_details = 0;
    parent_query->subquery_processed(this);
    return QUERY_SUCCESS;
}
