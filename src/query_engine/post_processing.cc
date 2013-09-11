/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "query.h"

using boost::assign::map_list_of;

// compare flow records based on UUID
bool PostProcessingQuery::flow_record_comparator(const QEOpServerProxy::OutRowT& lhs,
    const QEOpServerProxy::OutRowT& rhs) {
    std::map<std::string, std::string>::const_iterator lhs_it, rhs_it;
    lhs_it = lhs.find(g_viz_constants.UUID_KEY);
    QE_ASSERT(lhs_it != lhs.end());
    rhs_it = rhs.find(g_viz_constants.UUID_KEY);
    QE_ASSERT(rhs_it != rhs.end());
    if (lhs_it->second < rhs_it->second) return true;
    if (lhs_it->second > rhs_it->second) return false;

    return false;
}

bool PostProcessingQuery::sort_field_comparator(
        const std::map<std::string, std::string>& lhs,
        const std::map<std::string, std::string>& rhs) {
    std::map<std::string, std::string>::const_iterator lhs_it, rhs_it;
    for (std::vector<sort_field_t>::iterator sort_it = sort_fields.begin();
         sort_it != sort_fields.end(); sort_it++) {
        lhs_it = lhs.find((*sort_it).name);
        QE_ASSERT(lhs_it != lhs.end());
        rhs_it = rhs.find((*sort_it).name);
        QE_ASSERT(rhs_it != rhs.end());
        if ((*sort_it).type == std::string("int") || 
            (*sort_it).type == std::string("long") ||
            (*sort_it).type == std::string("ipv4")) {
            uint64_t lhs_val, rhs_val;
            stringToInteger(lhs_it->second, lhs_val);
            stringToInteger(rhs_it->second, rhs_val);
            if (lhs_val < rhs_val) return true;
            if (lhs_val > rhs_val) return false;
        } else {
            if (lhs_it->second < rhs_it->second) return true;
            if (lhs_it->second > rhs_it->second) return false;
        }
    }

    return false;
}

bool PostProcessingQuery::flowseries_merge_processing(
        const std::vector<QEOpServerProxy::OutRowT> *raw_result,
        std::vector<QEOpServerProxy::OutRowT> *merged_result) {
    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;

    switch(mquery->selectquery_->flowseries_query_type()) {
    case SelectQuery::FS_SELECT_STATS:
        fs_stats_merge_processing(raw_result, merged_result);
        break;
    case SelectQuery::FS_SELECT_FLOW_TUPLE_STATS:
        fs_tuple_stats_merge_processing(raw_result, merged_result);
        break;
    default:
        return false;
    }

    return true;
}

void PostProcessingQuery::fs_merge_stats(
            const QEOpServerProxy::OutRowT& input,
            QEOpServerProxy::OutRowT& output) {
    QEOpServerProxy::OutRowT::iterator pit = output.find(SELECT_SUM_PACKETS);
    if (pit != output.end()) {
        uint64_t sum_pkts;
        stringToInteger(pit->second, sum_pkts);
        uint64_t pkts;
        stringToInteger(input.find(SELECT_SUM_PACKETS)->second, pkts);
        sum_pkts += pkts;
        pit->second = integerToString(sum_pkts);
    }
    QEOpServerProxy::OutRowT::iterator bit = output.find(SELECT_SUM_BYTES);
    if (bit != output.end()) {
        uint64_t sum_bytes;
        stringToInteger(bit->second, sum_bytes);
        uint64_t bytes;
        stringToInteger(input.find(SELECT_SUM_BYTES)->second, bytes);
        sum_bytes += bytes;
        bit->second = integerToString(sum_bytes);
    }
}

void PostProcessingQuery::fs_stats_merge_processing(
        const std::vector<QEOpServerProxy::OutRowT> *raw_result, 
        std::vector<QEOpServerProxy::OutRowT> *merged_result) {
    if (!raw_result->size()) {
        return;
    }
    if (!merged_result->size()) {
        merged_result->reserve(raw_result->size());
        copy(raw_result->begin(), raw_result->end(),
             std::back_inserter(*merged_result));
        return;
    }
    assert(raw_result->size() == 1);
    assert(merged_result->size() == 1);
    QEOpServerProxy::OutRowT& mresult_row = merged_result->at(0);
    const QEOpServerProxy::OutRowT& rresult_row = raw_result->at(0);
    QE_TRACE(DEBUG, "fs_stats_merge_processing: merge_stats.");
    fs_merge_stats(rresult_row, mresult_row);
}

void PostProcessingQuery::fs_tuple_stats_merge_processing(
        const std::vector<QEOpServerProxy::OutRowT> *raw_result, 
        std::vector<QEOpServerProxy::OutRowT> *merged_result) {
    if (!raw_result->size()) {
        return;
    }
    if (!merged_result->size()) {
        merged_result->reserve(raw_result->size());
        copy(raw_result->begin(), raw_result->end(),
             std::back_inserter(*merged_result));
        return;
    }
    size_t merged_result_size = merged_result->size();
    for (size_t r = 0; r < raw_result->size(); ++r) {
        const QEOpServerProxy::OutRowT& rresult_row = raw_result->at(r);
        QEOpServerProxy::OutRowT::const_iterator rfc_it = 
            rresult_row.find(SELECT_FLOW_CLASS_ID);
        assert(rfc_it != rresult_row.end());
        uint64_t rfc_id;
        stringToInteger(rfc_it->second, rfc_id);
        size_t m;
        for (m = 0; m < merged_result_size; ++m) {
            QEOpServerProxy::OutRowT& mresult_row = merged_result->at(m);
            QEOpServerProxy::OutRowT::const_iterator mfc_it = 
                mresult_row.find(SELECT_FLOW_CLASS_ID);
            uint64_t mfc_id;
            stringToInteger(mfc_it->second, mfc_id);
            if (rfc_id == mfc_id) {
                fs_merge_stats(rresult_row, mresult_row);
                break;
            }
        }
        if (m == merged_result_size) {
            merged_result->push_back(rresult_row);
        }
    }
}

bool PostProcessingQuery::merge_processing(
        const QEOpServerProxy::BufferT& input, 
        QEOpServerProxy::BufferT& output)
{
    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;

    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return false;
    }

    // set the right table name
    output.first = input.first;

    if (mquery->table == g_viz_constants.FLOW_SERIES_TABLE) {
        if (flowseries_merge_processing(&input.second, &output.second)) {
            status_details = 0;
            return true;
        }
    }

    // Check if the result has to be sorted
    if (sorted) {
        std::vector<QEOpServerProxy::OutRowT> *merged_result =
            &output.second;

        const std::vector<QEOpServerProxy::OutRowT> *raw_result1 = 
            &(input.second);

        if (result_.get() == NULL)
        {
            merged_result->reserve(raw_result1->size());
            copy(raw_result1->begin(), raw_result1->end(), 
                std::back_inserter(*merged_result));
            goto sort_done;
        }

        std::vector<QEOpServerProxy::OutRowT> *raw_result2 = &(result_->second);
        size_t size1 = raw_result1->size();
        size_t size2 = raw_result2->size();
        QE_TRACE(DEBUG, "Merging results from vectors of size:" <<
                size1 << " and " << size2);
        merged_result->reserve(raw_result1->size() + raw_result2->size());
        if (sorting_type == ASCENDING) {
            std::merge(raw_result1->begin(), raw_result1->end(), 
                    raw_result2->begin(), raw_result2->end(),
                    std::back_inserter(*merged_result),
                      boost::bind(&PostProcessingQuery::sort_field_comparator, 
                                  this, _1, _2)); 
        } else {
            std::merge(raw_result1->rbegin(), raw_result1->rend(), 
                    raw_result2->rbegin(), raw_result2->rend(),
                    std::back_inserter(*merged_result),
                      boost::bind(&PostProcessingQuery::sort_field_comparator, 
                                  this, _1, _2)); 
        }

    } 

sort_done:
    if (!sorted && (mquery->table == g_viz_constants.FLOW_TABLE))
    {
        QE_TRACE(DEBUG, "Merge_Processing: Adding inputs to output");
        std::vector<QEOpServerProxy::OutRowT> *merged_result =
            &output.second;

        const std::vector<QEOpServerProxy::OutRowT> *raw_result1 = 
            &(input.second);

        if (result_.get() == NULL)
        {
            merged_result->reserve(raw_result1->size());
            copy(raw_result1->begin(), raw_result1->end(), 
                std::back_inserter(*merged_result));
        } else {

            std::vector<QEOpServerProxy::OutRowT> *raw_result2 = &(result_->second);
            size_t size1 = raw_result1->size();
            size_t size2 = raw_result2->size();
            QE_TRACE(DEBUG, "Merging results from vectors of size:" <<
                    size1 << " and " << size2);
            merged_result->reserve(raw_result1->size() + raw_result2->size());
            copy(raw_result1->begin(), raw_result1->end(), 
                std::back_inserter(*merged_result));
            copy(raw_result2->begin(), raw_result2->end(), 
                std::back_inserter(*merged_result));
        }
        QE_TRACE(DEBUG, "Merge_Processing: Done adding inputs to output");
    }

    // Have the result ready and processing is done
    status_details = 0;
    return true;
}

bool PostProcessingQuery::final_merge_processing(
const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
                        QEOpServerProxy::BufferT& output)
{
    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;

    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return false;
    }

    // set the right table name
    if (inputs.size() >= 1)
        output.first = inputs[0]->first;

    if (mquery->table == g_viz_constants.FLOW_SERIES_TABLE) {
        bool status = false;
        for (size_t i = 0; i < inputs.size(); i++) {
            status = flowseries_merge_processing(&inputs[i]->second, 
                                                 &output.second);
        }
        if (status) {
            if (sorted) {
                std::vector<QEOpServerProxy::OutRowT> *merged_result =
                    &output.second;
                if (sorting_type == ASCENDING) {
                    std::sort(merged_result->begin(), merged_result->end(), 
                              boost::bind(
                                &PostProcessingQuery::sort_field_comparator, 
                                          this, _1, _2)); 
                } else {
                    std::sort(merged_result->rbegin(), merged_result->rend(), 
                              boost::bind(
                                &PostProcessingQuery::sort_field_comparator,
                                          this, _1, _2));
                }
            }
            goto limit;
        }
    }

    if (mquery->table == g_viz_constants.FLOW_TABLE)
    {
        QE_TRACE(DEBUG, "Final_Merge_Processing: Uniquify flow records");
        // uniquify the records
        std::set<QEOpServerProxy::OutRowT, bool(*)(const QEOpServerProxy::OutRowT&, const QEOpServerProxy::OutRowT&)>
            result_row_set(&PostProcessingQuery::flow_record_comparator);

        for (size_t i = 0; i < inputs.size(); i++)
        {
            std::vector<QEOpServerProxy::OutRowT> *raw_result = 
                &inputs[i]->second;
            result_row_set.insert(raw_result->begin(), raw_result->end());
        }

        std::vector<QEOpServerProxy::OutRowT> *merged_result =
            &output.second;

        merged_result->reserve(result_row_set.size());
        copy(result_row_set.begin(), result_row_set.end(), 
            std::back_inserter(*merged_result));

        QE_TRACE(DEBUG, "Final_Merge_Processing: Done uniquify flow records");
        // Check if the result has to be sorted
        if (sorted) {
            if (sorting_type == ASCENDING) {
                std::sort(merged_result->begin(), merged_result->end(), 
                          boost::bind(&PostProcessingQuery::sort_field_comparator, 
                                      this, _1, _2)); 
            } else {
                std::sort(merged_result->rbegin(), merged_result->rend(), 
                          boost::bind(&PostProcessingQuery::sort_field_comparator,
                                      this, _1, _2));
            }
        }
    } else {  // For non-flow-record queries
        // Check if the result has to be sorted
        if (sorted) {
            std::vector<QEOpServerProxy::OutRowT> *merged_result =
                &output.second;

            size_t final_vector_size = 0;
            for (size_t i = 0; i < inputs.size(); i++)
                final_vector_size += inputs[i]->second.size();
        
            QE_TRACE(DEBUG, "Merging results between " << inputs.size() 
                    << " vectors with final vector size:" << final_vector_size);

            merged_result->reserve(final_vector_size);

            for (size_t i = 0; i < inputs.size(); i++)
            {
                std::vector<QEOpServerProxy::OutRowT> *raw_result = 
                    &inputs[i]->second;

                if (sorting_type == ASCENDING) {
                    std::vector<QEOpServerProxy::OutRowT>::iterator
                        current_result_end = merged_result->end();
                    copy(raw_result->begin(), raw_result->end(), 
                        std::back_inserter(*merged_result));
                    std::inplace_merge(merged_result->begin(),
                    current_result_end, merged_result->end(),
    boost::bind(&PostProcessingQuery::sort_field_comparator, this, _1, _2)); 
                } else {
                    std::vector<QEOpServerProxy::OutRowT>::reverse_iterator
                        current_result_end = merged_result->rbegin();
                    copy(raw_result->begin(), raw_result->end(), 
                        std::back_inserter(*merged_result));
                    std::inplace_merge(merged_result->rbegin(),
                    current_result_end, merged_result->rend(),
    boost::bind(&PostProcessingQuery::sort_field_comparator, this, _1, _2)); 
                }
            }
        }
    }
   
limit:
    if (limit) {
        std::vector<QEOpServerProxy::OutRowT> *merged_result =
            &output.second;
        QE_TRACE(DEBUG, "Apply Limit [" << limit << "]");
        if (merged_result->size() > (size_t)limit) {
            merged_result->resize(limit);
        }
    }

    // Have the result ready and processing is done
    status_details = 0;
    return true;
}

query_status_t PostProcessingQuery::process_query() {
    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return QUERY_FAILURE;
    }

    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;
    result_ = mquery->selectquery_->result_;
    std::vector<QEOpServerProxy::OutRowT> *raw_result = &result_->second;

    if (filter_list.size() != 0)
    {
        std::vector<std::map<std::string, std::string> > filtered_table;
        // do filter operation
        QE_TRACE(DEBUG, "Doing filter operation");
        for (size_t i = 0; i < raw_result->size(); i++)
        {
            bool delete_row = false;
            std::map<std::string, std::string> row = (*raw_result)[i];

            for (size_t j = 0; j < filter_list.size(); j++)
            {
                std::map<std::string, std::string>::iterator iter;
                iter = row.find(filter_list[j].name);
                if (iter == row.end())
                {
                    if (!(filter_list[j].ignore_col_absence))
                        delete_row = true;
                    break;
                }

                switch(filter_list[j].op)
                {
                    case EQUAL:
                        if (filter_list[j].value != iter->second)
                        {
                            delete_row = true;
                        }
                        break;

                    case NOT_EQUAL:
                        if (filter_list[j].value == iter->second)
                        {
                            delete_row = true;
                        }
                        break;

                    case LEQ:
                        {
                            int filter_value = 
                                atoi(filter_list[j].value.c_str());
                            int column_value= atoi(iter->second.c_str());
                            if (column_value > filter_value)
                            {
                                delete_row = true;
                            }
                            break;
                        }

                    case GEQ:
                        {
                            int filter_value = 
                                atoi(filter_list[j].value.c_str());
                            int column_value= atoi(iter->second.c_str());
                            if (column_value < filter_value)
                            {
                                delete_row = true;
                            }
                            break;
                        }

                    case REGEX_MATCH:
                        {
                            if (!boost::regex_match(iter->second, 
                                        filter_list[j].match_e))
                            {
                                delete_row = true;
                            }
                            break;
                        }

                    default:
                        // upsupported filter operation
                        QE_ASSERT(0);
                        break;
                }
                if (delete_row == true)
                    break;
            }
 
            if (delete_row == true)
            {
                QE_TRACE(DEBUG, "filter out entry #:" << i);
            } else {
                filtered_table.push_back(row);
            }
        }

        *raw_result = filtered_table;
    }

    // Check if the result has to be sorted
    if (sorted) {
        if (sorting_type == ASCENDING) {
            std::sort(raw_result->begin(), raw_result->end(), 
                      boost::bind(&PostProcessingQuery::sort_field_comparator, 
                                  this, _1, _2)); 
        } else {
            std::sort(raw_result->rbegin(), raw_result->rend(), 
                      boost::bind(&PostProcessingQuery::sort_field_comparator,
                                  this, _1, _2));
        }
    }

    // If the flow series query is parallelized, we should apply the limit 
    // only after the result from all the tasks are merged 
    // (@ final_merge_processing).
    if ((mquery->table != g_viz_constants.FLOW_SERIES_TABLE || 
        (mquery->table == g_viz_constants.FLOW_SERIES_TABLE && 
        !mquery->is_query_parallelized())) && limit) {
        QE_TRACE(DEBUG, "Apply Limit [" << limit << "]");
        if (raw_result->size() > (size_t)limit) {
            raw_result->resize(limit);
        }
    }

    if (IS_TRACE_ENABLED(POSTPROCESS_RESULT_TRACE))
    {
        std::vector<QEOpServerProxy::OutRowT>::iterator res_it;
        QE_TRACE(DEBUG, "== Post Processing Result ==");
        for (res_it = raw_result->begin(); res_it != raw_result->end(); ++res_it) {
            std::vector<final_result_col> row_entry;
            std::map<std::string, std::string>::iterator map_it;
            for (map_it = (*res_it).begin(); map_it != (*res_it).end(); ++map_it) {
                final_result_col col;
                col.set_col(map_it->first); col.set_value(map_it->second);
                row_entry.push_back(col);
                //QE_TRACE(DEBUG, map_it->first << " : " << map_it->second);
            }
            FINAL_RESULT_ROW_TRACE(QeTraceBuf, mquery->query_id, row_entry);
        }
    }

#if 0 
    //if ((limit) && (!sorted))
    for (int i = 0 ; i < 200000; i++)
    raw_result->push_back(map_list_of(
            "destvn","abc-cor\"poration:front-end-network:001")(
            "sourceip","168430090")("destip","3232238090")(
            "sourcevn","abc-corporation:front-end-network:002")(
            "protocol","80")("dport","62000")("sport","1000")(
            "sum(packets)","4294967196")
        );
#endif

    // Have the result ready and processing is done
    status_details = 0;
    parent_query->subquery_processed(this);
    return QUERY_SUCCESS;
}
