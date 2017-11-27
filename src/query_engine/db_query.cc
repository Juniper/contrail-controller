/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/tuple/tuple.hpp>
#include <boost/foreach.hpp>
#include "query.h"
#include "base/work_pipeline.h"

/*
 * This function performs GetRowAsync for each row key
 * Input: It takes rowkeys for which we have to get col
          values.Multiple instances execute this call
          simultaneously and possibly multiple times(steps)
          The instances store intermediate result in exts
          res stores consolidated result/instance
 * Ouput: Keeps calling itself for as many rowkeys involved
          Returns NULL if no more row key to be queried
*/
ExternalBase::Efn DbQueryUnit::QueryExec(uint32_t inst,
    const vector<q_result *> & exts,
    const Input & inp, Stage0Out & res) {

    uint32_t step = exts.size();
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    Input & cinp = const_cast<Input &> (inp);
    // Previous row fetch resulted in error, dont issue any more requests
    if (query_fetch_error) {
        return NULL;
    }
    if (step) {
        res.query_result.insert(res.query_result.end(),
                                exts[step-1]->begin(),
                                exts[step-1]->end());
    }
    res.current_row = cinp.row_count.fetch_and_increment();
    const uint32_t current_row = res.current_row;
    // continue as long as we have not reached end of rows
    if (current_row < inp.total_rows) {
        // Frame the getrow context
        GetRowInput *ip_ctx = new GetRowInput();
        ip_ctx->rowkey = cinp.keys[current_row];
        ip_ctx->cfname = cinp.cf_name;
        ip_ctx->crange = cinp.cr;
        ip_ctx->where_vec = cinp.where_vec;
        ip_ctx->chunk_no = m_query->parallel_batch_num;
        ip_ctx->qid = m_query->query_id;
        ip_ctx->sub_qid = sub_query_id;
        ip_ctx->row_no = current_row;
        ip_ctx->inst = inst;
        return boost::bind(&DbQueryUnit::PipelineCb, this, cinp.cf_name,
            cinp.keys[current_row], cinp.cr, cinp.where_vec, ip_ctx, _1);
    } else {
        // done processing fetching all rows
        return NULL;
    }
}

bool DbQueryUnit::PipelineCb(std::string &cfname, GenDb::DbDataValueVec &rowkey,
                           const GenDb::ColumnNameRange &cr,
                           GenDb::WhereIndexInfoVec &where_vec,
                           GetRowInput * ip_ctx, void *privdata) {

    // prepend T2: to value in each tuple in where_vec
    std::string T2_string = GenDb::DbDataValueToString(rowkey.at(0));
    BOOST_FOREACH(GenDb::WhereIndexInfo &where_info, where_vec) {
        std::string tempstr(T2_string);
        tempstr.append(":");
        tempstr.append(GenDb::DbDataValueToString(where_info.get<2>()));
        where_info.get<2>() = tempstr;
    }
    /*
     *  Call GetRowAsync, with args prepopulated
     */
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    return m_query->dbif_->Db_GetRowAsync(cfname, rowkey, cr, where_vec,
        GenDb::DbConsistency::LOCAL_ONE,
        boost::bind(&DbQueryUnit::cb, this, _1, _2, ip_ctx, privdata));
}

/*
 * This function merges all the instance's results into the res
 */
bool DbQueryUnit::QueryMerge(const std::vector<boost::shared_ptr<Stage0Out> >
                                  & subs,
                             const boost::shared_ptr<Input> & inp,
                             Output & res) {
    // Merge all the instances result. Each instance is a vector of
    // query_results from steps
    res.query_result = boost::shared_ptr<std::vector<query_result_unit_t> >
                               (new std::vector<query_result_unit_t>());
    for (vector<boost::shared_ptr<Stage0Out> >::const_iterator it =
             subs.begin(); it!=subs.end(); it++) {
        res.query_result->insert(res.query_result->end(),
                                (*it)->query_result.begin(),
                                (*it)->query_result.end());
    }
    return true;
}

/*
 * This function populates the rowkeys that have to be used
 * in querying.It populates it based on the cftype and the t2
 * and t1 values
 */
std::vector<GenDb::DbDataValueVec> DbQueryUnit::populate_row_keys() {
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    uint32_t t2_start = m_query->from_time() >> g_viz_constants.RowTimeInBits;
    uint32_t t2_end = m_query->end_time() >> g_viz_constants.RowTimeInBits;

    if (m_query->is_object_table_query(m_query->table()))
    {
        GenDb::DbDataValue timestamp_start =
            (uint32_t)std::numeric_limits<int32_t>::min();
        cr.start_.push_back(timestamp_start);
    }
    GenDb::DbDataValue timestamp_end =
        (uint32_t)std::numeric_limits<int32_t>::max();
    cr.finish_.push_back(timestamp_end);

    std::vector<GenDb::DbDataValueVec> keys;    // vector of keys for multi-row get
    GenDb::ColListVec mget_res;   // vector of result for each row
    for (uint32_t t2 = t2_start; t2 <= t2_end; t2++)
    {
        GenDb::ColList result;
        GenDb::DbDataValueVec rowkey;

        rowkey.push_back(t2);
        if (m_query->is_stat_table_query(m_query->table())) {
            uint8_t partition_no = 0;
            rowkey.push_back(partition_no);
        }
        if (m_query->is_flow_query(m_query->table()) ||
                m_query->is_session_query(m_query->table())) {
            for (uint8_t part_no = (uint8_t)g_viz_constants.PARTITION_MIN;
                     part_no < (uint8_t)g_viz_constants.PARTITION_MAX + 1;
                     part_no++) {
                GenDb::DbDataValueVec tmp_rowkey(rowkey);
                tmp_rowkey.push_back(part_no);
#ifdef USE_SESSION
                if (m_query->is_flow_query(m_query->table())) {
                    for (uint8_t is_si = 0; is_si < 2; is_si++) {
                        GenDb::DbDataValueVec tmp_rowkey2(tmp_rowkey);
                        tmp_rowkey2.push_back(is_si);
                        for (GenDb::DbDataValueVec::iterator it =
                                row_key_suffix.begin(); it!=row_key_suffix.end();
                                it++) {
                            GenDb::DbDataValueVec tmp_rowkey3(tmp_rowkey2);
                            tmp_rowkey3.push_back(*it);
                            keys.push_back(tmp_rowkey3);
                        }
                    }
                } else {
#endif
                    for (GenDb::DbDataValueVec::iterator it =
                            row_key_suffix.begin(); it!=row_key_suffix.end();
                            it++) {
                        tmp_rowkey.push_back(*it);
                    }
                    keys.push_back(tmp_rowkey);
#ifdef USE_SESSION
                }
#endif
            }
        } else {
            if (!t_only_row)
            {
                for (GenDb::DbDataValueVec::iterator it =
                        row_key_suffix.begin(); it!=row_key_suffix.end();
                        it++) {
                    rowkey.push_back(*it);
                }
            }

            // If querying message_index_tables, partition_no is an
            // additional row_key
            // It spans values 0..15
            if (t_only_col) {
                for (uint8_t part_no = (uint8_t)g_viz_constants.PARTITION_MIN;
                         part_no < (uint8_t)g_viz_constants.PARTITION_MAX + 1;
                         part_no++) {
                    GenDb::DbDataValueVec tmp_rowkey(rowkey);
                    tmp_rowkey.push_back(part_no);
                    keys.push_back(tmp_rowkey);
                }
            } else {
                keys.push_back(rowkey);
            }
        }
    }
    return keys;
}

/*
 * This function calls the GetRowAsync based on the input
 * passed to it. It creates a pipeline which executes
 * the multiple GetRow calls asynchronously. It also
 * provides a callback which is used to collect the
 * results from the GetRow operation
 */
query_status_t DbQueryUnit::process_query()
{
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    uint32_t t2_start = m_query->from_time() >> g_viz_constants.RowTimeInBits;
    uint32_t t2_end = m_query->end_time() >> g_viz_constants.RowTimeInBits;

    QE_TRACE(DEBUG,  " Async Database query for " <<
            (t2_end - t2_start + 1) << " rows");
    QE_TRACE(DEBUG,  " Async Database query for T2_start:"
            << t2_start
            << " T2_end:" << t2_end
            << " cf:" << cfname
            << " where_vec size:" << where_vec.size()
            << " column_start size:" << cr.start_.size()
            << " column_end size:" << cr.finish_.size()
            << " where_vec size:" << where_vec.size());
    std::vector<GenDb::DbDataValueVec> keys = populate_row_keys();

    /* Create a pipeline to fetch all rows corresponding to keys */
    int max_tasks = keys.size();
    std::vector<std::pair<int,int> > tinfo;
    for (uint idx=0; idx<(uint)max_tasks; idx++) {
        tinfo.push_back(make_pair(0, -1));
    }

    QEPipeT * wp = new QEPipeT(
        new WorkStage<Input, Output, q_result, Stage0Out>(
            tinfo,
            boost::bind(&DbQueryUnit::QueryExec, this, _1, _2, _3, _4),
            boost::bind(&DbQueryUnit::QueryMerge, this, _1, _2, _3)));

    // Populate the input to the pipeline
    boost::shared_ptr<Input> inp(new Input());
    inp.get()->row_count = 0;
    inp.get()->total_rows = keys.size();
    inp.get()->cf_name = cfname;
    inp.get()->cr = cr;
    inp.get()->where_vec = where_vec;
    inp.get()->keys = keys;
    // Start the pipeline with callback and input
    wp->Start(boost::bind(&DbQueryUnit::WPCompleteCb, this, wp, _1), inp);
    return QUERY_IN_PROGRESS;
}

/*
 * This is called after getting all the results from
 * all the row keys. It copies result to the query_result
 * and deletes the pipeline
 */
void DbQueryUnit::WPCompleteCb(QEPipeT *wp, bool ret_code) {
    boost::shared_ptr<Output> res = wp->Result();
    //copy pipeline output to DbQueryUnit query_output
    query_result = (res->query_result);
    int size = res->query_result->size();
    QE_TRACE(DEBUG,  " Database query completed with Async "
            << size << " rows");
    // Have the result ready and processing is done
    // sort the result before returning
    std::sort(query_result->begin(), query_result->end());
    if (IS_TRACE_ENABLED(WHERE_RESULT_TRACE)) {
        std::stringstream ss;
        for (std::vector<query_result_unit_t>::const_iterator it =
             query_result->begin(); it != query_result->end(); it++) {
            const query_result_unit_t &result_unit(*it);
            ss << "T: " << result_unit.timestamp << ": ";
            for (GenDb::DbDataValueVec::const_iterator rt =
                 result_unit.info.begin(); rt != result_unit.info.end();
                 rt++) {
                ss << " " << *rt;
            }
            ss << std::endl;
        }
        QE_TRACE(DEBUG, "Result: " << cfname << ": " << ss.str());
    }
    delete wp;
    if (ret_code && !query_fetch_error) {
        status_details = 0;
        query_status = QUERY_SUCCESS;
    } else {
        query_status = QUERY_FAILURE;
    }
    parent_query->subquery_processed(this);
}

void DbQueryUnit::cb(GenDb::DbOpResult::type dresult,
                     std::auto_ptr<GenDb::ColList> column_list,
                     GetRowInput * get_row_ctx, void * privdata) {
    std::auto_ptr<q_result> q_result_ptr(new q_result);
    std::auto_ptr<GetRowInput>  gri(get_row_ctx);
    uint32_t t2;
    uint8_t session_type = 0;
    uint8_t is_si = 0;
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    QueryEngine *qe = m_query->qe_;
    try {
        GenDb::DbDataValueVec val = gri.get()->rowkey;
        t2 = boost::get<uint32_t>(val.at(0));
#ifndef USE_SESSION
        if (m_query->is_session_query(m_query->table())) {
#else
        if (m_query->is_session_query(m_query->table())
            || m_query->is_flow_query(m_query->table())) {
#endif
            session_type = boost::get<uint8_t>(val.at(3));
            is_si = boost::get<uint8_t>(val.at(2));
        }
    } catch (boost::bad_get& ex) {
        assert(0);
    }
    if (dresult != GenDb::DbOpResult::OK) {
       // Dont issue any more requests
       query_fetch_error = true;
       ExternalProcIf<q_result> * rpi(
           reinterpret_cast<ExternalProcIf<q_result> *>(privdata));
       if (m_query->is_stat_table_query(m_query->table())) {
            tbb::mutex::scoped_lock lock(qe->smutex_);
            qe->stable_stats_.Update(m_query->stat_name_attr, false, true,
                false, 1);
       }
       rpi->Response(q_result_ptr);
       return;
    }
    // Update the reads against the stat
    if (m_query->is_stat_table_query(m_query->table())) {
        tbb::mutex::scoped_lock lock(qe->smutex_);
        qe->stable_stats_.Update(m_query->stat_name_attr, false, false,
            false, 1);
    }
    GenDb::NewColVec::iterator i;

    for (i = column_list->columns_.begin(); i != column_list->columns_.end();
         i++) {
        {
            query_result_unit_t result_unit;
            uint32_t t1;
            if (m_query->is_stat_table_query(m_query->table())) {
                assert(i->value->size()==1);
                assert((i->name->size()==4)||(i->name->size()==3));
                try {
                    t1 = boost::get<uint32_t>(i->name->at(i->name->size()-2));
                } catch (boost::bad_get& ex) {
                    assert(0);
                }
#ifndef USE_SESSION
            } else if (m_query->is_flow_query(m_query->table())) {
                int ts_at = i->name->size() - 2;
                assert(ts_at >= 0);
                try {
                    t1 = boost::get<uint32_t>(i->name->at(ts_at));
                } catch (boost::bad_get& ex) {
                    assert(0);
                }
            } else if (m_query->is_session_query(m_query->table())) {
#else
            } else if (m_query->is_session_query(m_query->table())
                      || m_query->is_flow_query(m_query->table())) {
#endif
                int ts_at = 2;
                try {
                    t1 = boost::get<uint32_t>(i->name->at(ts_at));
                } catch (boost::bad_get& ex) {
                    assert(0);
                }
            } else {
                // For MessageIndex tables t1 is stored in the first column
                // except for timestamp table
                int ts_at = 0;
                if (t_only_col) {
                    ts_at = i->name->size() - 2;
                } else {
                    ts_at = i->name->size() - 1;
                }
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
            if (m_query->is_stat_table_query(m_query->table())) {
                std::string attribstr;
                boost::uuids::uuid uuid;

                try {
                    uuid = boost::get<boost::uuids::uuid>(i->name->at(i->name->size()-1));
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
#ifndef USE_SESSION
            } else if (m_query->is_session_query(m_query->table())) {
#else
            } else if (m_query->is_session_query(m_query->table())
                      || m_query->is_flow_query(m_query->table())) {
#endif
                result_unit.info.clear();
                result_unit.info.push_back(is_si);
                result_unit.info.push_back(session_type);
                result_unit.info.push_back(i->name->at(0));
                result_unit.info.push_back(i->name->at(1));
                result_unit.info.push_back(i->name->at(2));
                result_unit.info.push_back(i->name->at(3));
                GenDb::DbDataValueVec::const_iterator itr;
                GenDb::DbDataValueVec::const_iterator end =
                    (m_query->selectquery_->unroll_needed?
                        (i->value->end()):(i->value->end() - 1));
                for (itr = i->value->begin(); itr != end;
                    itr++) {
                    result_unit.info.push_back(*itr);
                }
            } else {
                // If message index table uuid is not the value, but
                // column name
                if (t_only_col) {
                    GenDb::DbDataValueVec val = gri.get()->rowkey;
                    message_table_query_get_row(val, i, result_unit);
                } else {
                    result_unit.info = *i->value;
                }
            }
            q_result_ptr->push_back(result_unit);
        }

    }
    if (privdata) {
        ExternalProcIf<q_result> * rpi(
            reinterpret_cast<ExternalProcIf<q_result> *>(privdata));
        rpi->Response(q_result_ptr);
    }

}

void DbQueryUnit::message_table_query_get_row(
                                GenDb::DbDataValueVec const &val,
                                GenDb::NewColVec::iterator const &res_it,
                                query_result_unit_t &result_unit) {
    // cassandra returns fields in the ascending order by column-name.
    // pushing fields in order as per schema.
    // key      = rowkey[0]     T2
    // key2     = rowkey[1]     partition
    // column1  = name[0]       T1
    // column2  = name[1]       UUID
    // column3  = value[10]     T2:Source
    // column4  = value[11]     T2:Messagetype
    // column5  = value[12]     T2:ModuleId
    // column6  = value[13]     T2:<object-type1>:<object-value1>)
    // column7  = value[14]
    // column8  = value[15]
    // column9  = value[16]
    // column10 = value[0]
    // column11 = value[1]
    // column12 = value[2]
    // column13 = value[3]
    // column14 = value[4]
    // column15 = value[5]
    // column16 = value[6]
    // column17 = value[7]
    // column18 = value[8]
    // column19 = value[9]
    // DATA     = value[17]
    result_unit.info.push_back(val.at(0));
    result_unit.info.push_back(val.at(1));
    result_unit.info.push_back(res_it->name->at(0));
    result_unit.info.push_back(res_it->name->at(1));
    result_unit.info.push_back(res_it->value->at(10));
    result_unit.info.push_back(res_it->value->at(11));
    result_unit.info.push_back(res_it->value->at(12));
    result_unit.info.push_back(res_it->value->at(13));
    result_unit.info.push_back(res_it->value->at(14));
    result_unit.info.push_back(res_it->value->at(15));
    result_unit.info.push_back(res_it->value->at(16));
    result_unit.info.push_back(res_it->value->at(0));
    result_unit.info.push_back(res_it->value->at(1));
    result_unit.info.push_back(res_it->value->at(2));
    result_unit.info.push_back(res_it->value->at(3));
    result_unit.info.push_back(res_it->value->at(4));
    result_unit.info.push_back(res_it->value->at(5));
    result_unit.info.push_back(res_it->value->at(6));
    result_unit.info.push_back(res_it->value->at(7));
    result_unit.info.push_back(res_it->value->at(8));
    result_unit.info.push_back(res_it->value->at(9));
    result_unit.info.push_back(res_it->value->at(17));
}
