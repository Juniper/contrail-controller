/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

/*
 * This file will not contain actual query processing code but instead only
 * the code to 
 * a) Interact with external interfaces like REDIS etc.
 * b) Parse JSON strings passed to populate the query structures
 */

#include "rapidjson/document.h"
#include "base/logging.h"
#include "query.h"
#include <boost/assign/list_of.hpp>
#include <cerrno>
#include "analytics/vizd_table_desc.h"
#include "stats_select.h"
#include "stats_query.h"
#include <base/connection_info.h>

using std::map;
using std::string;
using boost::assign::map_list_of;
using boost::system::error_code;
using process::ConnectionState;
using process::ConnectionType;
using process::ConnectionStatus;

GenDb::GenDbIf* query_result_unit_t::dbif = NULL;
int QueryEngine::max_slice_ = 100;

typedef  std::vector< std::pair<std::string, std::string> > spair_vector;
static spair_vector query_string_to_column_name(0);

std::string get_column_name(std::string query_string)
{
    spair_vector::iterator iter; 

    for (iter = query_string_to_column_name.begin();
            iter != query_string_to_column_name.end();
            iter++)
    {
        if (iter->first == query_string)
            return iter->second;
    }

    return query_string;
}

std::string get_query_string(std::string column_name)
{
    spair_vector::iterator iter; 

    for (iter = query_string_to_column_name.begin();
            iter != query_string_to_column_name.end();
            iter++)
    {
        if (iter->second == column_name)
            return iter->first;
    }

    return column_name;
}

QueryResultMetaData::~QueryResultMetaData() {
}

PostProcessingQuery::PostProcessingQuery(
    std::map<std::string, std::string>& json_api_data,
    QueryUnit *main_query) :  QueryUnit(main_query, main_query), 
        sorted(false), limit(0) {
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    std::map<std::string, std::string>::iterator iter;

    QE_TRACE(DEBUG, __func__ );

    json_string_ = "";

    for (iter = json_api_data.begin(); iter != json_api_data.end(); iter++)
    {
        if (iter->first == QUERY_SORT_OP)
        {
            sorted = true;
            int tmp;
            std::istringstream(iter->second) >> tmp; 
            sorting_type = (sort_op)tmp;
            m_query->merge_needed = true;
            QE_TRACE(DEBUG, "sorting_type :" << sorting_type);
            json_string_ += iter->second;
            json_string_ += " ";
        }
        
        if (iter->first == QUERY_LIMIT)
        {
            std::istringstream(iter->second) >> limit;
            m_query->merge_needed = true;
            QE_TRACE(DEBUG, "limit :"<< limit);
            json_string_ += iter->second;
            json_string_ += " ";
        }

        if (iter->first == QUERY_SORT_FIELDS)
        {
            rapidjson::Document d;
            std::string json_string = "{ \"sort_fields\" : " + 
                iter->second + " }";
            json_string_ += json_string;
            json_string_ += " ";

            d.Parse<0>(const_cast<char *>(json_string.c_str()));
            const rapidjson::Value& json_sort_fields =
                d["sort_fields"]; 
            QE_PARSE_ERROR(json_sort_fields.IsArray());
            QE_TRACE(DEBUG, "# of sort fields:"<< json_sort_fields.Size());
            for (rapidjson::SizeType i = 0; i<json_sort_fields.Size(); i++) 
            {
                QE_PARSE_ERROR(json_sort_fields[i].IsString());
                std::string sort_str(json_sort_fields[i].GetString());
                QE_TRACE(DEBUG, sort_str);
                std::string datatype(m_query->get_column_field_datatype(sort_str));
                if (!m_query->is_stat_table_query()) {
                    QE_INVALIDARG_ERROR(datatype != std::string(""));
                } else if (m_query->stats().is_stat_table_static()) {
                    // This is a static StatTable. We can check the schema
                    QE_INVALIDARG_ERROR(datatype != std::string(""));
                }
                QE_INVALIDARG_ERROR(
                    m_query->is_valid_sort_field(sort_str) != false);
                sort_field_t sort_field(get_column_name(sort_str), datatype);
                sort_fields.push_back(sort_field);
            }
        }

        /*
         * old filter style is just list of expr ANDed
         * new filter are list of ANDs over OR
         * both modes are supported with the below code
         */
        if (iter->first == QUERY_FILTER) {
            rapidjson::Document d;
            std::string json_string = "{ \"filter\" : " + 
                iter->second + " }";
            json_string_ += json_string;
            json_string_ += " ";

            d.Parse<0>(const_cast<char *>(json_string.c_str()));
            const rapidjson::Value& json_filters =
                d["filter"]; 
            QE_PARSE_ERROR(json_filters.IsArray());
            QE_TRACE(DEBUG, "# of filters:"<< json_filters.Size());
            bool single_list = false;
            if (json_filters.Size()) {
                rapidjson::SizeType zeroth = 0;
                const rapidjson::Value& json_filters_0 = json_filters[zeroth];
                if (!json_filters_0.IsArray()) {
                    single_list = true;
                }
            }

            if (single_list) {
                //parse the old format 
                std::vector<filter_match_t> filter_and;
                for (rapidjson::SizeType j = 0; j<json_filters.Size(); j++) 
                  {
                    filter_match_t filter;

                    const rapidjson::Value& name_value = 
                        json_filters[j][WHERE_MATCH_NAME];
                    const rapidjson::Value&  value_value = 
                        json_filters[j][WHERE_MATCH_VALUE];
                    const rapidjson::Value& op_value = 
                        json_filters[j][WHERE_MATCH_OP];

                    // do some validation checks
                    QE_INVALIDARG_ERROR(name_value.IsString());
                    QE_INVALIDARG_ERROR
                        ((value_value.IsString() || value_value.IsNumber()));
                    QE_INVALIDARG_ERROR(op_value.IsNumber());

                    filter.name = name_value.GetString();
                    filter.op = (match_op)op_value.GetInt();

                    // extract value after type conversion
                      {
                        if (value_value.IsString())
                          {
                            filter.value = value_value.GetString();
                          } else if (value_value.IsInt()){
                              int int_value;
                              std::ostringstream convert;
                              int_value = value_value.GetInt();
                              convert << int_value;
                              filter.value = convert.str();
                          } else if (value_value.IsUint()) {
                              uint32_t uint_value;
                              std::ostringstream convert;
                              uint_value = value_value.GetUint();
                              convert << uint_value;
                              filter.value = convert.str();
                          }
                      }

                    if (filter.op == REGEX_MATCH)
                      {
                        // compile regex beforehand
                        filter.match_e = boost::regex(filter.value);
                      }

                    filter_and.push_back(filter);
                  }
                filter_list.push_back(filter_and);
            } else {
                //new OR of ANDs
                for (rapidjson::SizeType j = 0; j<json_filters.Size(); j++) {
                    std::vector<filter_match_t> filter_and;
                    const rapidjson::Value& json_filter_and = json_filters[j];
                    QE_PARSE_ERROR(json_filter_and.IsArray());

                    for (rapidjson::SizeType k = 0; k<json_filter_and.Size(); k++) {
                        filter_match_t filter;

                        const rapidjson::Value& name_value = 
                            json_filter_and[k][WHERE_MATCH_NAME];
                        const rapidjson::Value&  value_value = 
                            json_filter_and[k][WHERE_MATCH_VALUE];
                        const rapidjson::Value& op_value = 
                            json_filter_and[k][WHERE_MATCH_OP];

                        // do some validation checks
                        QE_INVALIDARG_ERROR(name_value.IsString());
                        QE_INVALIDARG_ERROR
                            ((value_value.IsString() || value_value.IsNumber()));
                        QE_INVALIDARG_ERROR(op_value.IsNumber());

                        filter.name = name_value.GetString();
                        filter.op = (match_op)op_value.GetInt();

                        // extract value after type conversion
                        if (value_value.IsString()) {
                            filter.value = value_value.GetString();
                        } else if (value_value.IsInt()) {
                            int int_value;
                            std::ostringstream convert;
                            int_value = value_value.GetInt();
                            convert << int_value;
                            filter.value = convert.str();
                        } else if (value_value.IsUint()) {
                            uint32_t uint_value;
                            std::ostringstream convert;
                            uint_value = value_value.GetUint();
                            convert << uint_value;
                            filter.value = convert.str();
                        }

                        if (filter.op == REGEX_MATCH) {
                            // compile regex beforehand
                            filter.match_e = boost::regex(filter.value);
                        }

                        filter_and.push_back(filter);
                    }
                    filter_list.push_back(filter_and);
                }
            }
        }
    }

    // add filter to filter query engine logs if requested
    if ((((AnalyticsQuery *)main_query)->filter_qe_logs) &&
            (((AnalyticsQuery *)main_query)->table() == 
             g_viz_constants.COLLECTOR_GLOBAL_TABLE)) {
        QE_TRACE(DEBUG,  " Adding filter for QE logs");
        filter_match_t filter;
        filter.name = g_viz_constants.MODULE;
        filter.value = 
            ((AnalyticsQuery *)main_query)->sandesh_moduleid;
        filter.op = NOT_EQUAL;
        filter.ignore_col_absence = true;
        if (!filter_list.size()) {
            std::vector<filter_match_t> filter_and;
            filter_and.push_back(filter);
            filter_list.push_back(filter_and);
        } else {
            for (unsigned int i = 0; i < filter_list.size(); i++) {
                filter_list[i].push_back(filter);
            }
        }
    }

    // If the user has specified the sorting field and not the sorting order,
    // then sort the result in ascending order.
    if (sort_fields.size() && sorted == false) {
        sorted = true;
        sorting_type = ASCENDING; 
    }
}

bool AnalyticsQuery::merge_processing(
    const QEOpServerProxy::BufferT& input, 
    QEOpServerProxy::BufferT& output) {

    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return false;
    }

    // Have the result ready and processing is done
    status_details = 0;
    return postprocess_->merge_processing(input, output);
}

bool AnalyticsQuery::final_merge_processing(
const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
    QEOpServerProxy::BufferT& output) {

    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return false;
    }

    // Have the result ready and processing is done
    status_details = 0;
    return postprocess_->final_merge_processing(inputs, output);
}

// this is to get parallelization details once the query is parsed
void AnalyticsQuery::get_query_details(bool& is_merge_needed, bool& is_map_output,
        std::vector<uint64_t>& chunk_sizes,
        std::string& where,
        std::string& select,
        std::string& post,
        uint64_t& time_period,
        int& parse_status)
{
    QE_TRACE(DEBUG, "time_slice is " << time_slice);
    if (status_details == 0)
    {
        for (uint64_t chunk_start = original_from_time; 
                chunk_start < original_end_time; chunk_start += time_slice)
        {
            if ((chunk_start+time_slice) <= original_end_time) {
                chunk_sizes.push_back(time_slice);
            } else {
                chunk_sizes.push_back((original_end_time - chunk_start));
            }
        }
    } else {
        chunk_sizes.push_back(0); // just return some dummy value
    }

    time_period = (end_time_ - from_time_) / 1000000;

    parse_status = status_details;
    if (parse_status != 0) return;
    
    if (is_stat_table_query()) {
        is_merge_needed = selectquery_->stats_->IsMergeNeeded();
    } else {
        is_merge_needed = merge_needed;
    }

    where = wherequery_->json_string_;
    select = selectquery_->json_string_;
    post = postprocess_->json_string_;
    is_map_output = is_stat_table_query();
}

bool AnalyticsQuery::can_parallelize_query() {
    parallelize_query_ = true;
    if (table_ == g_viz_constants.OBJECT_VALUE_TABLE) {
        parallelize_query_ = false;
    }
    return parallelize_query_;
}

void AnalyticsQuery::Init(GenDb::GenDbIf *db_if, std::string qid,
    std::map<std::string, std::string>& json_api_data, 
    uint64_t analytics_start_time)
{
    std::map<std::string, std::string>::iterator iter;

    QE_TRACE(DEBUG, __func__);
    
    // populate fields 
    query_id = qid;

    // Initialize database
    query_result_unit_t::dbif = db_if;
    dbif = db_if;
    QE_IO_ERROR(dbif != NULL)

    sandesh_moduleid = 
        g_vns_constants.ModuleNames.find(Module::QUERY_ENGINE)->second;
    {
        std::stringstream json_string; json_string << " { ";
        for (std::map<std::string, 
                std::string>::iterator it = json_api_data_.begin();
                it != json_api_data_.end(); it++) {
            json_string << 
                ((it != json_api_data_.begin())? " , " : "") <<
                it->first << ": " << it->second;
        }
        json_string << " } ";
        QE_LOG_GLOBAL(DEBUG, "json query is: " << json_string.str());
    }

    // parse JSON query
    // FROM field
    {
        iter = json_api_data.find(QUERY_TABLE);
        QE_PARSE_ERROR(iter != json_api_data.end());

        //strip " from the passed string
        table_ = iter->second.substr(1, iter->second.size()-2);

        // boost::to_upper(table);
        QE_TRACE(DEBUG,  " table is " << table_);
        if (StatsQuery::is_stat_table_query(table_)) {
            stats_.reset(new StatsQuery(table_));
        }
        QE_INVALIDARG_ERROR(is_valid_from_field(table_));
    }

    // Start time
    {
        iter = json_api_data.find(QUERY_START_TIME);
        QE_PARSE_ERROR(iter != json_api_data.end());
        req_from_time_ = parse_time(iter->second);
        QE_TRACE(DEBUG,  " from_time is " << req_from_time_);
        if (req_from_time_ < analytics_start_time) 
        {
            from_time_ = analytics_start_time;
            QE_TRACE(DEBUG, "updated start_time to:" << from_time_);
        } else {
            from_time_ = req_from_time_;
        }
    }

    // End time
    {
        struct timeval curr_time; 
        gettimeofday(&curr_time, NULL);

        iter = json_api_data.find(QUERY_END_TIME);
        QE_PARSE_ERROR(iter != json_api_data.end());
        req_end_time_ = parse_time(iter->second);
        QE_TRACE(DEBUG,  " end_time is " << req_end_time_);

        if (req_end_time_ < analytics_start_time) {
            end_time_ = analytics_start_time;
        } else if (req_end_time_ > 
                (uint64_t)(curr_time.tv_sec*1000000+curr_time.tv_usec)) {
            end_time_ = curr_time.tv_sec*1000000+curr_time.tv_usec;
            QE_TRACE(DEBUG, "updated end_time to:" << end_time_);
        } else {
            end_time_ = req_end_time_;
        }
    }

    // Initialize SELECT/WHERE/Post-Processing components of query
    // for input validation

    // where processing initialization
    std::string where_json_string;
    {
        int direction = INGRESS;
        iter = json_api_data.find(QUERY_FLOW_DIR);
        if (iter != json_api_data.end()) {
            std::istringstream(iter->second) >> direction;
            QE_TRACE(DEBUG,  "set flow direction to:" << direction);
        }
        
        iter = json_api_data.find(QUERY_WHERE);
        if (iter == json_api_data.end())
        {
            QE_TRACE(DEBUG, "Where * query");
            where_json_string = std::string("");
        } else {
            where_json_string = iter->second;
        }

        QE_TRACE(DEBUG,  " Initializing Where Query");
        wherequery_ = new WhereQuery(where_json_string, direction, this);
        this->status_details = wherequery_->status_details;
        if (this->status_details != 0 )
        {
            QE_LOG_GLOBAL(DEBUG, "Error in WHERE parsing");
            return;
        }
    }

    // select processing initialization
    {
        QE_TRACE(DEBUG,  " Initializing Select Query");
        selectquery_ = new SelectQuery(this, json_api_data);
        this->status_details = selectquery_->status_details;
        if (this->status_details != 0 )
        {
            QE_LOG_GLOBAL(DEBUG, "Error in SELECT parsing");
            return;
        }
        /*
         * ObjectId queries are special, they are requested from Object* tables,
         * but the values are extrated from g_viz_constants.OBJECT_VALUE_TABLE
         */
        if (is_object_table_query()) {
            if (selectquery_->ObjectIdQuery()) {
                object_value_key = table_;
                table_ = g_viz_constants.OBJECT_VALUE_TABLE;
            }
        }
    }

    // post processing initialization
    QE_TRACE(DEBUG,  " Initializing PostProcessing Query");
    postprocess_ = new PostProcessingQuery(json_api_data, this);
    this->status_details = postprocess_->status_details;
    if (this->status_details != 0 )
    {
        QE_LOG_GLOBAL(DEBUG, "Error in PostProcess parsing");
        return;
    }

    if (this->is_stat_table_query()) {
        selectquery_->stats_->SetSortOrder(postprocess_->sort_fields);
    }

    // just to take care of issues with Analytics start time 
         if (from_time_ > end_time_)
            from_time_ = end_time_ - 1; 

    // Get the right job slice for parallelization
    original_from_time = from_time_;
    original_end_time = end_time_;

    if (can_parallelize_query()) {
        uint64_t smax = pow(2,g_viz_constants.RowTimeInBits) * \
              QueryEngine::max_slice_;

        time_slice = ((end_time_ - from_time_)/total_parallel_batches) + 1;

        if (time_slice < (uint64_t)pow(2,g_viz_constants.RowTimeInBits)) {
            time_slice = pow(2,g_viz_constants.RowTimeInBits);
        }
        if (time_slice > smax) {
            time_slice = smax;
        }          
        // Adjust the time_slice for Flowseries query, if time granularity is 
        // specified. Divide the query based on the time granularity.
        if (selectquery_->provide_timeseries && selectquery_->granularity) {
            if (selectquery_->granularity >= time_slice) {
                time_slice = selectquery_->granularity;
            } else {
                time_slice = ((time_slice/selectquery_->granularity)+1)*
                    selectquery_->granularity;
            }
        }

        uint8_t fs_query_type = selectquery_->flowseries_query_type();
        if ((table() == g_viz_constants.FLOW_TABLE) || 
            (table() == g_viz_constants.FLOW_SERIES_TABLE &&
             (fs_query_type == SelectQuery::FS_SELECT_STATS || 
              fs_query_type == SelectQuery::FS_SELECT_FLOW_TUPLE_STATS))) {
            merge_needed = true;
        }

        QE_TRACE(DEBUG, "time_slice:" << time_slice << " , # of parallel "
                "batches:" << total_parallel_batches);

    } else {
        // No parallelization
        QE_LOG_GLOBAL(DEBUG, "No parallelization for this query");
        merge_needed = false;
        parallelize_query_ = false;
        time_slice = end_time_ - from_time_;
    }

    from_time_ = 
        original_from_time + time_slice*parallel_batch_num;
    end_time_ = from_time_ + time_slice;
    if (from_time_ >= original_end_time)
    {
        processing_needed = false;
    } else if (end_time_ > original_end_time) {
        end_time_ = original_end_time;
    }

    if (processing_needed)
    {
        // change it to trace later TBD
        QE_TRACE(DEBUG, "For batch:" << parallel_batch_num << " from_time:" << from_time_ << " end_time:" << end_time_ << " time slice:" << time_slice);
    } else {
        QE_TRACE(DEBUG, "No processing needed for batch:" << parallel_batch_num);
    }

}

/*
 * time could be in now +/-10m/h/s format.Parse that and return th
 * UTC corresponding to the parsed val
 */
uint64_t AnalyticsQuery::parse_time(const std::string& relative_time)
{
    uint64_t offset_usec = 0;
    std::string temp;
    if (!relative_time.compare("\"now\"")) {
        return UTCTimestampUsec();
    } else if (!(relative_time.substr(1,3)).compare("now")) {
        //Find the offset to be shifted
        int found = 0;
    //Extract any number after now
        if ((found = relative_time.find_last_of("h")) > 0) {
            std::istringstream(relative_time.substr(5,found-4)) >> offset_usec;
            offset_usec = offset_usec*3600*1000000;
        } else if ((found = relative_time.find_last_of("m")) > 0) {
            std::istringstream(relative_time.substr(5,found-4)) >> offset_usec;
            offset_usec = offset_usec*60*1000000;
        } else if ((found = relative_time.find_last_of("s")) > 0) {
            std::istringstream(relative_time.substr(5,found-4)) >> offset_usec;
            offset_usec = offset_usec*1000000;
        } else {
            QE_LOG_GLOBAL(DEBUG, "Error in time parsing.h/m/s expected");   
            return 0;
        }

        //If now+ return UTC + offset else UTC - offset 
        if (!(relative_time.substr(1,4)).compare("now+")) {
            return (UTCTimestampUsec() + offset_usec);
        } else if (!(relative_time.substr(1,4)).compare("now-")) {
            return (UTCTimestampUsec() - offset_usec);
        } else {
            QE_LOG_GLOBAL(DEBUG, "Error in time parsing. now+/-expected");
            return 0;
        }
    } else {
        //To handle old version of input where integer is parsed
        std::istringstream(relative_time) >> offset_usec;
        return offset_usec;
    }
}


QueryUnit::QueryUnit(QueryUnit *p_query, QueryUnit *m_query):
    parent_query(p_query), main_query(m_query), pending_subqueries(0),
    query_status(QUERY_PROCESSING_NOT_STARTED), status_details(0) 
{
    if (p_query)
        p_query->sub_queries.push_back(this);
};

QueryUnit::~QueryUnit()
{
    int num_sub_queries = sub_queries.size();
    for(int i = 0; i<num_sub_queries; i++)
        delete sub_queries[i];
}


// Get UUID from the info field
void query_result_unit_t::get_uuid(boost::uuids::uuid& u)
{
    try {
        u = boost::get<boost::uuids::uuid>(info.at(0));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    }
}

// Get UUID and stats
void query_result_unit_t::get_uuid_stats(boost::uuids::uuid& u, 
        flow_stats& stats)
{
    try {
        stats.bytes = boost::get<uint64_t>(info.at(0));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    }

    try {
        stats.pkts = boost::get<uint64_t>(info.at(1));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    }

    try {
        stats.short_flow = (boost::get<uint8_t>(info.at(2)) == 1)? true : false;
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    }

    try {
        u = boost::get<boost::uuids::uuid>(info.at(3));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    }

    return;
}

void query_result_unit_t::set_stattable_info(
        const std::string& attribstr,
        const boost::uuids::uuid& uuid) {
    info.push_back(attribstr);
    info.push_back(uuid);
}

void  query_result_unit_t::get_stattable_info(
            std::string& attribstr,
            boost::uuids::uuid& uuid) {

    int index = 0;

    try {
        attribstr = boost::get<std::string>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }

    try {
        uuid = boost::get<boost::uuids::uuid>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }

}

// Get UUID and stats and 8-tuple
void query_result_unit_t::get_uuid_stats_8tuple(boost::uuids::uuid& u,
       flow_stats& stats, flow_tuple& tuple)
{
    int index = 0;
    try {
        stats.bytes = boost::get<uint64_t>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }

    try {
        stats.pkts = boost::get<uint64_t>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }

    try {
        stats.short_flow = (boost::get<uint8_t>(info.at(index++)) == 1? true : false);
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }

    try {
        u = boost::get<boost::uuids::uuid>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }

    try {
        tuple.vrouter = boost::get<std::string>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }

    try {
        tuple.source_vn = boost::get<std::string>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }

    try {
        tuple.dest_vn = boost::get<std::string>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }
    try {
        tuple.source_ip = boost::get<uint32_t>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }
    try {
        tuple.dest_ip = boost::get<uint32_t>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }
    try {
        tuple.protocol = boost::get<uint8_t>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }
    try {
        tuple.source_port = boost::get<uint16_t>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }
    try {
        tuple.dest_port = boost::get<uint16_t>(info.at(index++));
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }
    return;
}

query_status_t AnalyticsQuery::process_query()
{
    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return QUERY_FAILURE;
    }

    QE_TRACE(DEBUG, "Start Where Query Processing");
    where_start_ = UTCTimestampUsec();
    query_status = wherequery_->process_query();
    qperf_.chunk_where_time =
            static_cast<uint32_t>((UTCTimestampUsec() - where_start_)/1000);

    status_details = wherequery_->status_details;
    if (query_status != QUERY_SUCCESS) 
    {
        QE_LOG(DEBUG, "where processing failed with error:"<< query_status);
        return query_status;
    }
    QE_TRACE(DEBUG, "End Where Query Processing");

    QE_TRACE(DEBUG, "Start Select Processing");
    select_start_ = UTCTimestampUsec();
    query_status = selectquery_->process_query();
    status_details = selectquery_->status_details;
    qperf_.chunk_select_time =
            static_cast<uint32_t>((UTCTimestampUsec() - select_start_)/1000);

    if (query_status != QUERY_SUCCESS)
    {
        QE_LOG(DEBUG, 
                "select processing failed with error:"<< query_status);
        return query_status;
    }
    QE_TRACE(DEBUG, "End Select Processing. row #s:" << 
            selectquery_->result_->size());

    QE_TRACE(DEBUG, "Start PostProcessing");
    postproc_start_ = UTCTimestampUsec();
    query_status = postprocess_->process_query();
    status_details = postprocess_->status_details;
    qperf_.chunk_postproc_time =
            static_cast<uint32_t>((UTCTimestampUsec() - postproc_start_)/1000);

    final_result = postprocess_->result_;
    final_mresult = postprocess_->mresult_;
    if (query_status != QUERY_SUCCESS)
    {
        QE_LOG(DEBUG, 
                "post processing failed with error:"<< query_status);
        return query_status;
    }
    QE_TRACE(DEBUG, "End PostProcessing. row #s:" << 
            final_result->size());

    return QUERY_SUCCESS;
}

AnalyticsQuery::AnalyticsQuery(std::string qid, std::map<std::string, 
        std::string>& json_api_data, uint64_t analytics_start_time,
        EventManager *evm, std::vector<std::string> cassandra_ips, 
        std::vector<int> cassandra_ports, int batch,
        int total_batches):
        QueryUnit(NULL, this),
        dbif_(GenDb::GenDbIf::GenDbIfImpl(
            boost::bind(&AnalyticsQuery::db_err_handler, this),
            cassandra_ips, cassandra_ports, 0, "QueryEngine", true)),
        filter_qe_logs(true),
        json_api_data_(json_api_data),
        where_start_(0),
        select_start_(0),
        postproc_start_(0),        
        merge_needed(false),
        parallel_batch_num(batch),
        total_parallel_batches(total_batches),
        processing_needed(true),
        stats_(NULL)
{
    // Need to do this for logging/tracing with query ids
    query_id = qid;

    QE_TRACE(DEBUG, __func__);

    // Initialize database connection
    QE_TRACE(DEBUG, "Initializing database");
    dbif = dbif_.get();

    boost::system::error_code ec;
    if (!dbif->Db_Init("qe::DbHandler", -1)) {
        QE_LOG(ERROR, "Database initialization failed");
        this->status_details = EIO;
    }

    if (!dbif->Db_SetTablespace(g_viz_constants.COLLECTOR_KEYSPACE)) {
        QE_LOG(ERROR,  ": Create/Set KEYSPACE: " <<
           g_viz_constants.COLLECTOR_KEYSPACE << " FAILED");
        this->status_details = EIO;
    }   
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_tables.begin();
            it != vizd_tables.end(); it++) {
        if (!dbif_->Db_UseColumnfamily(*it)) {
            QE_LOG(ERROR, "Database initialization:Db_UseColumnfamily failed");
            this->status_details = EIO;
        }
    }
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_flow_tables.begin();
            it != vizd_flow_tables.end(); it++) {
        if (!dbif_->Db_UseColumnfamily(*it)) {
            QE_LOG(ERROR, "Database initialization:Db_UseColumnfamily failed");
            this->status_details = EIO;
        }
    }
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_stat_tables.begin();
            it != vizd_stat_tables.end(); it++) {
        if (!dbif_->Db_UseColumnfamily(*it)) {
            QE_LOG(ERROR, "Database initialization:Db_UseColumnfamily failed");
            this->status_details = EIO;
        }
    }
    boost::asio::ip::address db_addr(boost::asio::ip::address::from_string(
        dbif->Db_GetHost(), ec));
    boost::asio::ip::tcp::endpoint db_endpoint(db_addr, dbif->Db_GetPort());
    if (this->status_details != 0) {
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DATABASE,
            std::string(), ConnectionStatus::DOWN, db_endpoint, std::string());
    } else {
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DATABASE,
            std::string(), ConnectionStatus::UP, db_endpoint, std::string());
    }
    dbif->Db_SetInitDone(true);
    Init(dbif, qid, json_api_data, analytics_start_time);
}

AnalyticsQuery::AnalyticsQuery(std::string qid, GenDb::GenDbIf *dbif,
    std::map<std::string, std::string> json_api_data, 
    uint64_t analytics_start_time, int batch, int total_batches) :
    QueryUnit(NULL, this),
    dbif_(dbif),
    query_id(qid),
    json_api_data_(json_api_data),
    where_start_(0), 
    select_start_(0), 
    postproc_start_(0),
    merge_needed(false),
    parallel_batch_num(batch),
    total_parallel_batches(total_batches),
    processing_needed(true),
    stats_(NULL) {
    Init(dbif, qid, json_api_data, analytics_start_time);
}

QueryEngine::QueryEngine(EventManager *evm,
            const std::string & redis_ip, unsigned short redis_port,
            int max_tasks, int max_slice, uint64_t anal_ttl) :    
        qosp_(new QEOpServerProxy(evm,
            this, redis_ip, redis_port, max_tasks)),
        evm_(evm),
        cassandra_ports_(0)
{
    max_slice_ =  max_slice;
    init_vizd_tables();

    // Initialize database connection
    QE_LOG_NOQID(DEBUG, "Initializing QE without database!");

    uint64_t curr_time = UTCTimestampUsec();
    QE_LOG_NOQID(DEBUG, "Could not find analytics start time");
    uint64_t ttl = anal_ttl*60*60*1000000;
    stime = curr_time - ttl;
    QE_LOG_NOQID(DEBUG, "set stime to " << stime << "and AnalyticsTTL to " << g_viz_constants.AnalyticsTTL);
}

QueryEngine::QueryEngine(EventManager *evm,
            std::vector<std::string> cassandra_ips,
            std::vector<int> cassandra_ports,
            const std::string & redis_ip, unsigned short redis_port,
            int max_tasks, int max_slice, uint64_t anal_ttl, 
            uint64_t start_time) :  
        dbif_(GenDb::GenDbIf::GenDbIfImpl( 
            boost::bind(&QueryEngine::db_err_handler, this),
            cassandra_ips, cassandra_ports, 0, "QueryEngine", true)),
        qosp_(new QEOpServerProxy(evm,
            this, redis_ip, redis_port, max_tasks)),
        evm_(evm),
        cassandra_ports_(cassandra_ports),
        cassandra_ips_(cassandra_ips)
{
    max_slice_ = max_slice;
    init_vizd_tables();

    // Initialize database connection
    QE_TRACE_NOQID(DEBUG, "Initializing database");
    GenDb::GenDbIf *db_if = dbif_.get();

    boost::system::error_code ec;
    int retries = 0;
    bool retry = true;
    while (retry == true) {
        retry = false;

        if (!db_if->Db_Init("qe::DbHandler", -1)) {
            QE_LOG_NOQID(ERROR, "Database initialization failed");
            retry = true;
        }

        if (!retry) {
            if (!db_if->Db_SetTablespace(g_viz_constants.COLLECTOR_KEYSPACE)) {
                QE_LOG_NOQID(ERROR,  ": Create/Set KEYSPACE: " <<
                        g_viz_constants.COLLECTOR_KEYSPACE << " FAILED");
                retry = true;
            }
        }

        if (!retry) {
            for (std::vector<GenDb::NewCf>::const_iterator it = vizd_tables.begin();
                    it != vizd_tables.end(); it++) {
                if (!dbif_->Db_UseColumnfamily(*it)) {
                    retry = true;
                    break;
                }
            }
        }

        if (!retry) {
            for (std::vector<GenDb::NewCf>::const_iterator it = vizd_flow_tables.begin();
                    it != vizd_flow_tables.end(); it++) {
                if (!dbif_->Db_UseColumnfamily(*it)) {
                    retry = true;
                    break;
                }
            }
        }
        if (retry) {
            std::stringstream ss;
            ss << "initialization of database failed. retrying " << retries++ << " time";
            // Update connection info
            boost::asio::ip::address db_addr(boost::asio::ip::address::from_string(
                db_if->Db_GetHost(), ec));
            boost::asio::ip::tcp::endpoint db_endpoint(db_addr, db_if->Db_GetPort());
            ConnectionState::GetInstance()->Update(ConnectionType::DATABASE,
                std::string(), ConnectionStatus::DOWN, db_endpoint, std::string());
            Q_E_LOG_LOG("QeInit", SandeshLevel::SYS_WARN, ss.str());
            db_if->Db_Uninit("qe::DbHandler", -1);
            sleep(5);
        }
    }
    if (start_time != 0) {
        stime = start_time;
    } else {
        bool init_done = false;
        retries = 0;
        while (!init_done && retries < 5) {
            GenDb::ColList col_list;
            std::string cfname = g_viz_constants.SYSTEM_OBJECT_TABLE;
            GenDb::DbDataValueVec key;
            key.push_back(g_viz_constants.SYSTEM_OBJECT_ANALYTICS);

            if (dbif_->Db_GetRow(col_list, cfname, key)) {
                for (GenDb::NewColVec::iterator it = col_list.columns_.begin();
                        it != col_list.columns_.end(); it++) {
                    std::string col_name;
                    try {
                        col_name = boost::get<std::string>(it->name->at(0));
                    } catch (boost::bad_get& ex) {
                        QE_LOG_NOQID(ERROR, __func__ << ": Exception on col_name get");
                    }

                    if (col_name == g_viz_constants.SYSTEM_OBJECT_START_TIME) {
                        try {
                            stime = boost::get<uint64_t>(it->value->at(0));
                            init_done = true;
                        } catch (boost::bad_get& ex) {
                            QE_LOG_NOQID(ERROR, __func__ << "Exception for boost::get, what=" << ex.what());
                            break;
                        }
                    }
                }
            }
            retries++;
            if (!init_done)
                sleep(5);
        }
        if (!init_done) {
            uint64_t ttl = anal_ttl*60*60*1000000;
            uint64_t curr_time = UTCTimestampUsec();
            stime = curr_time - ttl;
            QE_LOG_NOQID(ERROR, __func__ << "setting start_time manually to" << stime);
        }
    }
    dbif_->Db_SetInitDone(true);
    // Update connection info
    boost::asio::ip::address db_addr(boost::asio::ip::address::from_string(
        db_if->Db_GetHost(), ec));
    boost::asio::ip::tcp::endpoint db_endpoint(db_addr, db_if->Db_GetPort());
    ConnectionState::GetInstance()->Update(ConnectionType::DATABASE,
        std::string(), ConnectionStatus::UP, db_endpoint, std::string());
}

using std::vector;

int
QueryEngine::QueryPrepare(QueryParams qp,
        std::vector<uint64_t> &chunk_size,
        bool & need_merge, bool & map_output,
        std::string& where, std::string& select, std::string& post,
        uint64_t& time_period, 
        std::string &table) {
    string& qid = qp.qid;
    QE_LOG_NOQID(INFO, 
             " Got Query to prepare for QID " << qid);
    int ret_code;
    if (cassandra_ports_.size() == 1 && cassandra_ports_[0] == 0) {
        chunk_size.push_back(999);
        need_merge = false;
        map_output = false;
        ret_code = 0;
        table = string("ObjectCollectorInfo");
    } else {

        AnalyticsQuery *q = new AnalyticsQuery(qid, qp.terms, stime, evm_,
                cassandra_ips_, cassandra_ports_, 0, qp.maxChunks);
        chunk_size.clear();
        q->get_query_details(need_merge, map_output, chunk_size,
            where, select, post, time_period, ret_code);
        table = q->table();
        delete q;
    }
    return ret_code;
}

bool
QueryEngine::QueryAccumulate(QueryParams qp,
        const QEOpServerProxy::BufferT& input,
        QEOpServerProxy::BufferT& output) {

    QE_TRACE_NOQID(DEBUG, "Creating analytics query object for merge_processing");
    AnalyticsQuery *q = new AnalyticsQuery(qp.qid, qp.terms, stime, evm_,
        cassandra_ips_, cassandra_ports_, 1, qp.maxChunks);
    QE_TRACE_NOQID(DEBUG, "Calling merge_processing");
    bool ret = q->merge_processing(input, output);
    delete q;
    return ret;
}

bool
QueryEngine::QueryFinalMerge(QueryParams qp,
        const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
        QEOpServerProxy::BufferT& output) {

    QE_TRACE_NOQID(DEBUG, "Creating analytics query object for final_merge_processing");
    AnalyticsQuery *q = new AnalyticsQuery(qp.qid, qp.terms, stime, evm_,
        cassandra_ips_, cassandra_ports_, 1, qp.maxChunks);
    QE_TRACE_NOQID(DEBUG, "Calling final_merge_processing");
    bool ret = q->final_merge_processing(inputs, output);
    delete q;
    return ret;
}

bool
QueryEngine::QueryFinalMerge(QueryParams qp,
        const std::vector<boost::shared_ptr<QEOpServerProxy::OutRowMultimapT> >& inputs,
        QEOpServerProxy::OutRowMultimapT& output) {
    QE_TRACE_NOQID(DEBUG, "Creating analytics query object for final_merge_processing");
    AnalyticsQuery *q = new AnalyticsQuery(qp.qid, qp.terms, stime, evm_,
        cassandra_ips_, cassandra_ports_, 1, qp.maxChunks);

    if (!q->is_stat_table_query()) {
        QE_TRACE_NOQID(DEBUG, "MultiMap merge_final is for Stats only");
        delete q;
        return false;
    }
    QE_TRACE_NOQID(DEBUG, "Calling final_merge_processing for Stats");

    q->selectquery_->stats_->MergeFinal(inputs, output);
    delete q;
    return true;   
}

bool 
QueryEngine::QueryExec(void * handle, QueryParams qp, uint32_t chunk)
{
    string& qid = qp.qid;
    QE_TRACE_NOQID(DEBUG,
             " Got Query to execute for QID " << qid << " chunk:"<< chunk);
    //GenDb::GenDbIf *db_if = dbif_.get();
    if (cassandra_ports_.size() == 1 && cassandra_ports_[0] == 0) {
        std::auto_ptr<QEOpServerProxy::BufferT> final_output(new QEOpServerProxy::BufferT);
        QEOpServerProxy::OutRowT outrow = boost::assign::map_list_of(
            "MessageTS", "1368037623434740")(
            "Messagetype", "IFMapString")(
            "ModuleId", "ControlNode")(
            "Source","b1s1")(
            "ObjectLog","\n<IFMapString type=\"sandesh\"><message type=\"string\" identifier=\"1\">Cancelling Response timer.</message><file type=\"string\" identifier=\"-32768\">src/ifmap/client/ifmap_state_machine.cc</file><line type=\"i32\" identifier=\"-32767\">578</line></IFMapString>");
        QEOpServerProxy::MetadataT metadata;
        std::auto_ptr<QEOpServerProxy::OutRowMultimapT> final_moutput(new QEOpServerProxy::OutRowMultimapT);
        for (int i = 0 ; i < 100; i++)
            final_output->push_back(std::make_pair(outrow, metadata));
        QE_TRACE_NOQID(DEBUG, " Finished query processing for QID " << qid << " chunk:" << chunk);
        QEOpServerProxy::QPerfInfo qperf(0,0,0);
        qperf.error = 0;
        qosp_->QueryResult(handle, qperf, final_output, final_moutput);
        return true;
    }

    AnalyticsQuery *q = new AnalyticsQuery(qid, qp.terms, stime, evm_,
            cassandra_ips_, cassandra_ports_, chunk, qp.maxChunks);

    QE_TRACE_NOQID(DEBUG, " Finished parsing and starting processing for QID " << qid << " chunk:" << chunk); 
    q->process_query(); 

    QE_TRACE_NOQID(DEBUG, " Finished query processing for QID " << qid << " chunk:" << chunk);
    q->qperf_.error = q->status_details;
    qosp_->QueryResult(handle, q->qperf_, q->final_result, q->final_mresult);
    delete q;
    return true;
}


std::ostream &operator<<(std::ostream &out, query_result_unit_t& res)
{
    out << "T:" << res.timestamp << " : Need to extract other information";
#if 0
    out << "T:" << res.timestamp << " : ";

    if (res.info.length() < 48) {
        boost::uuids::uuid tmp_u;
        res.get_uuid(tmp_u);
        out << " UUID:" << tmp_u;
    } else if (res.info.length() == 48) {
        boost::uuids::uuid tmp_u; flow_stats tmp_stats; 
        res.get_uuid_stats(tmp_u, tmp_stats);
        out << " UUID:" << tmp_u << 
            " Bytes: " << tmp_stats.bytes <<
            " Pkts: " << tmp_stats.pkts <<
            " Short-Flow: " << tmp_stats.short_flow;
    } else if (res.info.length() > 48) {
        boost::uuids::uuid tmp_u; flow_stats tmp_stats; 
            flow_tuple tmp_tuple;
        res.get_uuid_stats_8tuple(tmp_u, tmp_stats, tmp_tuple);
        out << " UUID:" << tmp_u << 
            " SVN: " << tmp_tuple.source_vn <<
            " DVN: " << tmp_tuple.dest_vn<<
            " SIP: " << tmp_tuple.source_ip <<
            " DIP: " << tmp_tuple.dest_ip <<
            " PROTO: " << tmp_tuple.protocol <<
            " SPORT: " << tmp_tuple.source_port <<
            " DPORT: " << tmp_tuple.dest_port <<
            " DIR: " << tmp_tuple.direction <<
            " Bytes: " << tmp_stats.bytes <<
            " Pkts: " << tmp_stats.pkts <<
            " Short-Flow: " << tmp_stats.short_flow;
    }
#endif

    return out;
}

// validation functions
bool AnalyticsQuery::is_object_table_query()
{
    return (
        (this->table_ != g_viz_constants.COLLECTOR_GLOBAL_TABLE) &&
        (this->table_ != g_viz_constants.FLOW_TABLE) &&
        (this->table_ != g_viz_constants.FLOW_SERIES_TABLE) &&
        (this->table_ != g_viz_constants.OBJECT_VALUE_TABLE) &&
        !is_stat_table_query());
}


bool AnalyticsQuery::is_valid_from_field(const std::string& from_field)
{
    for(size_t i = 0; i < g_viz_constants._TABLES.size(); i++)
    {
        if (g_viz_constants._TABLES[i].name == from_field)
            return true;
    }

    for (std::map<std::string, objtable_info>::const_iterator it =
            g_viz_constants._OBJECT_TABLES.begin();
            it != g_viz_constants._OBJECT_TABLES.end(); it++) {
        if (it->first == from_field)
            return true;
    }
    if (is_stat_table_query())
        return true;

    return false;
}

bool AnalyticsQuery::is_valid_where_field(const std::string& where_field)
{
    for(size_t i = 0; i < g_viz_constants._TABLES.size(); i++)
    {
        if (g_viz_constants._TABLES[i].name == table_)
        {
            for (size_t j = 0; 
                j < g_viz_constants._TABLES[i].schema.columns.size(); j++)
            {
                if ((g_viz_constants._TABLES[i].schema.columns[j].name ==
                        where_field) &&
                        g_viz_constants._TABLES[i].schema.columns[j].index)
                    return true;
            }
            return false;
        }
    }
    for (std::map<std::string, objtable_info>::const_iterator it =
            g_viz_constants._OBJECT_TABLES.begin();
            it != g_viz_constants._OBJECT_TABLES.end(); it++) {
        if (it->first == table_)
        {
            for (size_t j = 0; j < g_viz_constants._OBJECT_TABLE_SCHEMA.columns.size(); j++)
            {
                if ((g_viz_constants._OBJECT_TABLE_SCHEMA.columns[j].name == where_field) &&
                        g_viz_constants._OBJECT_TABLE_SCHEMA.columns[j].index)
                    return true;
            }
            return false;
        }
    }

    if (is_stat_table_query()) {
        AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
        if (m_query->stats().is_stat_table_static()) {
            StatsQuery::column_t cdesc = m_query->stats().get_column_desc(where_field);
            if (cdesc.index) return true;
        } else {
            // For dynamic Stat Table queries, allow anything in the where clause
            return true;
        }
    }
    return false;
}

bool AnalyticsQuery::is_valid_sort_field(const std::string& sort_field) {
    if (
        (sort_field == SELECT_PACKETS) ||
        (sort_field == SELECT_BYTES) ||
        (sort_field == SELECT_SUM_PACKETS) ||
        (sort_field == SELECT_SUM_BYTES)
        )
        return true;

    return selectquery_->is_present_in_select_column_fields(sort_field);
}

std::string AnalyticsQuery::get_column_field_datatype(
                                    const std::string& column_field) {
    for(size_t i = 0; i < g_viz_constants._TABLES.size(); i++) {
        if (g_viz_constants._TABLES[i].name == table_) {
            for (size_t j = 0; 
                 j < g_viz_constants._TABLES[i].schema.columns.size(); j++) {
                if (g_viz_constants._TABLES[i].schema.columns[j].name == 
                        column_field) {
                    return g_viz_constants._TABLES[i].schema.columns[j].datatype;
                }
            }
            return std::string("");
        }
    }
    for (std::map<std::string, objtable_info>::const_iterator it =
            g_viz_constants._OBJECT_TABLES.begin();
            it != g_viz_constants._OBJECT_TABLES.end(); it++) {
        if (it->first == table_) {
            for (size_t j = 0; j < g_viz_constants._OBJECT_TABLE_SCHEMA.columns.size(); j++) {
                if (g_viz_constants._OBJECT_TABLE_SCHEMA.columns[j].name == column_field) {
                    return g_viz_constants._OBJECT_TABLE_SCHEMA.columns[j].datatype;
                }
            }
            return std::string("");
        }
    }
    if (stats_.get()) {
        StatsQuery::column_t vt = stats().get_column_desc(column_field);
        if (vt.datatype == QEOpServerProxy::STRING)
            return string("string");
        else if (vt.datatype == QEOpServerProxy::UINT64)
            return string("int");
        else if (vt.datatype == QEOpServerProxy::DOUBLE)
            return string("double");
        else
            return string("");
    }
    return std::string("");
}

bool AnalyticsQuery::is_flow_query()
{
    return ((this->table_ == g_viz_constants.FLOW_SERIES_TABLE) ||
        (this->table_ == g_viz_constants.FLOW_TABLE));
}

#define UNIT_TEST_MESSAGE_FILTERS

void QueryEngine::QueryEngine_Test()
{
#if 0
    GenDb::GenDbIf *db_if = dbif_.get();

    // Create the query first
    std::string qid("TEST-QUERY");
    std::map<std::string, std::string> json_api_data;

#ifdef UNIT_TEST_MESSAGES
    QE_TRACE_NOQID(DEBUG,  " Testing messages ");
    QE_TRACE_NOQID(DEBUG,  " Parsing sample query");
        // Flow-Series Query
        json_api_data.insert(std::pair<std::string, std::string>(
                    "table", "\"MessageTable\""
        ));

        json_api_data.insert(std::pair<std::string, std::string>(
        "start_time", "1363918451328671"
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "end_time",   "1363918651330163" 
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "where", "[[{\"name\":\"Source\", \"value\":\"127.0.0.1\", \"op\":1} , {\"name\":\"Messagetype\", \"value\":\"UveVirtualMachineConfigTrace\", \"op\":1} ]]"
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"Module\", \"Source\", \"Messagetype\"]"
        ));

        AnalyticsQuery *q = 
            new AnalyticsQuery(db_if, qid, json_api_data, "0");
    QE_TRACE_NOQID(DEBUG,  " Parsing of messages query done");  

    QE_TRACE_NOQID(DEBUG,  " Invoking messages query");  
        q->process_query();
    QE_TRACE_NOQID(DEBUG,  " Processed messages query");  
#endif

#ifdef UNIT_TEST_MESSAGE_FILTERS
    QE_TRACE_NOQID(DEBUG,  " Testing messages ");
    QE_TRACE_NOQID(DEBUG,  " Parsing sample query");
        // Flow-Series Query
        json_api_data.insert(std::pair<std::string, std::string>(
                    "table", "\"MessageTable\""
        ));

        json_api_data.insert(std::pair<std::string, std::string>(
        "start_time", "1365021325382585"
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "end_time",   "1365025325382585" 
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"ModuleId\", \"Source\", \"Level\", \"Messagetype\"]"
        ));
        json_api_data.insert(std::pair<std::string, std::string>("filter", "[{\"name\":\"Level\", \"value\":\"6\", \"op\":5}]"));

#if 0
        AnalyticsQuery *q = 
            new AnalyticsQuery(db_if, qid, json_api_data, "0");
#endif
    QE_TRACE_NOQID(DEBUG,  " Parsing of messages query done");  

    QE_TRACE_NOQID(DEBUG,  " Invoking messages query");  
        q->process_query();
    QE_TRACE_NOQID(DEBUG, "# of rows: " << q->final_result->size());
    QE_TRACE_NOQID(DEBUG,  " Processed messages query");  
#endif


#ifdef UNIT_TEST_FLOW_SERIES
    QE_TRACE_NOQID(DEBUG,  " Testing flow series");
    QE_TRACE_NOQID(DEBUG,  " Parsing sample query");
        // Flow-Series Query
        json_api_data.insert(std::pair<std::string, std::string>(
                    "table", "\"FlowSeriesTable\""
        ));

        json_api_data.insert(std::pair<std::string, std::string>(
        "start_time", "1363816971234206"
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "end_time",   "1363816971284356" 
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "where", "[[{\"name\":\"sourcevn\", \"value\":\"default-domain:admin:vn0\", \"op\":1},{\"name\":\"destvn\", \"value\":\"default-domain:admin:vn1\", \"op\":1}],[{\"name\":\"protocol\", \"value\":\"17\", \"op\":1},{\"name\":\"dport\", \"value\":\"80\", \"op\":1}]]"
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "select_fields", "[\"T=5\", \"sourcevn\", \"destvn\", \"sum(packets)\"]"
        ));
        json_api_data.insert(std::pair<std::string, std::string>(
        "dir", "1"
        ));

        AnalyticsQuery *q = 
            new AnalyticsQuery(db_if, qid, json_api_data, "0");
    QE_TRACE_NOQID(DEBUG,  " Parsing of flow series query done");  

    QE_TRACE_NOQID(DEBUG,  " Invoking flow series query");  
        q->process_query();
    QE_TRACE_NOQID(DEBUG,  " Processed flow series query");  
#endif
#endif

}

std::map< std::string, int > trace_enable_map;
void TraceEnable::HandleRequest() const
{
    if (get_enable())
    {
        std::string trace_type = get_TraceType();
        trace_enable_map.insert(std::make_pair(trace_type, 0));
    } else {
        trace_enable_map.erase(get_TraceType());
    }
}

std::ostream& operator<<(std::ostream& out, const flow_tuple& ft) {
    out << ft.vrouter << ":" << ft.source_vn << ":"  
        << ft.dest_vn << ":" << ft.source_ip << ":"
        << ft.dest_ip << ":" << ft.protocol << ":"
        << ft.source_port << ":" << ft.dest_port << ":"
        << ft.direction;
    return out;
}
