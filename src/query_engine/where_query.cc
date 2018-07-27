/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cstdlib>
#include <limits>
#include <string>
#include <sstream>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include "rapidjson/document.h"
#include <boost/foreach.hpp>
#include "query.h"
#include "json_parse.h"
#include "base/regex.h"
#include "base/string_util.h"
#include "database/gendb_constants.h"
#include "database/gendb_if.h"
#include "utils.h"
#include "query.h"
#include "stats_query.h"

using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;
using std::string;

static std::string ToString(const contrail_rapidjson::Value& value_value) {
    std::string svalue;
    if (value_value.IsString())
    {
        svalue = value_value.GetString();
    } else if (value_value.IsInt()){
        int int_value;
        std::ostringstream convert;
        int_value = value_value.GetInt();
        convert << int_value;
        svalue = convert.str();
    } else if (value_value.IsUint()) {
        uint32_t uint_value;
        std::ostringstream convert;
        uint_value = value_value.GetUint();
        convert << uint_value;
        svalue = convert.str();
    } else if (value_value.IsDouble()) {
        double dbl_value;
        std::ostringstream convert;
        dbl_value = value_value.GetDouble();
        convert << dbl_value;
        svalue = convert.str();
    }
    return svalue;
}

static GenDb::DbDataValue ToDbDataValue(const std::string& value, QEOpServerProxy::VarType desc) {
    GenDb::DbDataValue smpl;
    if (desc == QEOpServerProxy::STRING) {
        smpl = value;
    } else if (desc == QEOpServerProxy::UINT64) {
        smpl = (uint64_t) strtoul(value.c_str(), NULL, 10);
    } else if (desc == QEOpServerProxy::DOUBLE) {
        smpl = (double) strtod(value.c_str(), NULL); 
    }
    return smpl;
}

static GenDb::DbDataValue ToDbDataValue(const contrail_rapidjson::Value& val) {
    GenDb::DbDataValue ret;
    if (val.IsString()) {
        ret = std::string(val.GetString());
    } else if (val.IsUint()) {
        ret = (uint64_t) val.GetUint();
    } else if (val.IsInt()) {
        ret = (uint64_t) val.GetInt();
    } else if (val.IsDouble()) {
        ret = (double) val.GetDouble();
    }
    return ret;
}

static QEOpServerProxy::VarType ToDbDataType(string val) {
    QEOpServerProxy::VarType ret = QEOpServerProxy::BLANK;
    if (val == "int") {
        ret = QEOpServerProxy::UINT64;
    } else if (val == "string") {
        ret = QEOpServerProxy::STRING;
    } else if (val == "uuid") {
        ret = QEOpServerProxy::UUID;
    } else if (val == "double") {
        ret = QEOpServerProxy::DOUBLE;
    }
    return ret;
}

static StatsQuery::column_t get_column_desc(std::map<std::string,StatsQuery::column_t> table_schema, std::string pname) {
    StatsQuery::column_t cdesc;
    std::map<std::string,StatsQuery::column_t>::const_iterator st =
            table_schema.find(pname);
    if (st!=table_schema.end()) {
        cdesc = st->second;
    } else {
        cdesc.datatype = QEOpServerProxy::BLANK;
        cdesc.index = false;
        cdesc.output = false;
    }
    return cdesc;
}

bool
WhereQuery::StatTermParse(QueryUnit *main_query, const contrail_rapidjson::Value& where_term,
        std::string& pname, match_op& pop, GenDb::DbDataValue& pval, GenDb::DbDataValue& pval2,
        std::string& sname, match_op& sop, GenDb::DbDataValue& sval, GenDb::DbDataValue& sval2) {

    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    QE_ASSERT(m_query->is_stat_table_query(m_query->table()));

    contrail_rapidjson::Document dd;
    std::string srvalstr, srval2str;

    if (!where_term.HasMember(WHERE_MATCH_NAME))
        return false;
    const contrail_rapidjson::Value& name_value = where_term[WHERE_MATCH_NAME];
    if (!name_value.IsString()) return false;
    pname = name_value.GetString();

    const contrail_rapidjson::Value& prval = where_term[WHERE_MATCH_VALUE];
    if (!((prval.IsString() || prval.IsNumber()))) return false;
    contrail_rapidjson::Value prval2;
    if (where_term.HasMember(WHERE_MATCH_VALUE2)) {
        prval2.CopyFrom(where_term[WHERE_MATCH_VALUE2], dd.GetAllocator());
    }

    // For dynamic stat tables, convert types as per query json
    pval = ToDbDataValue(prval);
    pval2 = ToDbDataValue(prval2);

    if (!where_term.HasMember(WHERE_MATCH_OP))
        return false;
    const contrail_rapidjson::Value& op_value = where_term[WHERE_MATCH_OP];
    if (!op_value.IsNumber()) return false;
    pop = (match_op)op_value.GetInt();

    QE_TRACE(DEBUG, "StatTable Where Term Prefix " << pname << " val " << ToString(prval) 
            << " val2 " << ToString(prval2) << " op " << pop);

    sname = std::string(); 
    sop = (match_op)0;
    if (where_term.HasMember(WHERE_MATCH_SUFFIX)) {
        const contrail_rapidjson::Value& suffix = where_term[WHERE_MATCH_SUFFIX];
        if (suffix.IsObject()) {
            // For prefix-suffix where terms, prefix operator MUST be "EQUAL"
            if (pop != EQUAL) return false;

            // For prefix-suffix where terms, prefix value2 MUST be Null
            if (!prval2.IsNull()) return false;

            if (!suffix.HasMember(WHERE_MATCH_VALUE))
                return false;
            const contrail_rapidjson::Value& svalue_value =
                suffix[WHERE_MATCH_VALUE];
            if (!((svalue_value.IsString() || svalue_value.IsNumber()))) return false;
            srvalstr = ToString(svalue_value);
            // For dynamic stat tables, convert types as per query json
            sval = ToDbDataValue(svalue_value);

            contrail_rapidjson::Value svalue2_value;
            if (suffix.HasMember(WHERE_MATCH_VALUE2)) {
                svalue2_value.CopyFrom(suffix[WHERE_MATCH_VALUE2],
                                       dd.GetAllocator());
            }
            srval2str = ToString(svalue2_value);
            // For dynamic stat tables, convert types as per query json
            sval2 = ToDbDataValue(svalue2_value);

            if (!suffix.HasMember(WHERE_MATCH_OP))
                return false;
            const contrail_rapidjson::Value& sop_value =
                suffix[WHERE_MATCH_OP];
            if (!sop_value.IsNumber()) return false;
            sop = (match_op)sop_value.GetInt();

            if (!suffix.HasMember(WHERE_MATCH_NAME))
                return false;
            const contrail_rapidjson::Value& sname_value =
                suffix[WHERE_MATCH_NAME];
            if (!sname_value.IsString()) return false;
            sname = sname_value.GetString();
        }
        QE_TRACE(DEBUG, "StatTable Where Term Suffix" << sname << " val " <<
                 srvalstr << " val2 " << srval2str << " op " << sop);
    }

    StatsQuery::column_t cdesc;
    cdesc.datatype = QEOpServerProxy::BLANK;
    std::map<std::string, StatsQuery::column_t> table_schema;
    if (m_query->stats().is_stat_table_static()) {
        // For static tables, check that prefix is valid and convert types as per schema
        cdesc = m_query->stats().get_column_desc(pname);
    } else {
        // Get the stable schema from query if sent
        AnalyticsQuery *aQuery = (AnalyticsQuery *)m_query;
        std::map<std::string, std::string>::iterator iter, iter2;
        iter = aQuery->json_api_data_.find(QUERY_TABLE_SCHEMA);
        if (iter != aQuery->json_api_data_.end()) {
            contrail_rapidjson::Document d;
            std::string json_string = "{ \"schema\" : " + iter->second + " }";
            d.Parse<0>(const_cast<char *>(json_string.c_str()));
            const contrail_rapidjson::Value& json_schema = d["schema"];
            // If schema is not passed, proceed without suffix information
            if (json_schema.Size() == 0) {
                return true;
            }
            for (contrail_rapidjson::SizeType j = 0; j<json_schema.Size(); j++) {
                if (!(json_schema[j].HasMember(WHERE_MATCH_NAME) &&
                      json_schema[j].HasMember(QUERY_TABLE_SCHEMA_DATATYPE) &&
                      json_schema[j].HasMember(QUERY_TABLE_SCHEMA_INDEX) &&
                      json_schema[j].HasMember(QUERY_TABLE_SCHEMA_SUFFIXES)))
                    return false;
                const contrail_rapidjson::Value& name = json_schema[j][WHERE_MATCH_NAME];
                const contrail_rapidjson::Value&  datatype =
                        json_schema[j][QUERY_TABLE_SCHEMA_DATATYPE];
                const contrail_rapidjson::Value& index =
                        json_schema[j][QUERY_TABLE_SCHEMA_INDEX];
                const contrail_rapidjson::Value& suffixes =
                        json_schema[j][QUERY_TABLE_SCHEMA_SUFFIXES];
                StatsQuery::column_t cdesc;
                std::string vstr = datatype.GetString();
                cdesc.datatype = ToDbDataType(vstr);
                cdesc.index = index.GetBool()? true : false;

                if (suffixes.IsArray() && suffixes.Size() > 0) {
                    for (contrail_rapidjson::SizeType k = 0; k<suffixes.Size(); k++) {
                        const contrail_rapidjson::Value& suffix_name = suffixes[k];
                        cdesc.suffixes.insert(suffix_name.GetString());
                    }
                }
                table_schema[name.GetString()] = cdesc;
            }
        }
        cdesc = get_column_desc(table_schema, pname);
    }

    if (cdesc.datatype == QEOpServerProxy::BLANK) return false;
    if (!cdesc.index) return false;

    QE_TRACE(DEBUG, "StatTable Where prefix Schema match " << cdesc.datatype);
    // Now fill in the prefix value and value2 based on types in schema
    std::string vstr = ToString(prval);
    pval = ToDbDataValue(vstr, cdesc.datatype);
    if (!prval2.IsNull()) {
        std::string vstr = ToString(prval2);
        pval2 = ToDbDataValue(vstr, cdesc.datatype);
    }

    if (cdesc.suffixes.empty()) {
        // We need to use a onetag cf as the index
        if (!sname.empty()) return false;
        if (sop) return false;
        if (!srvalstr.empty()) return false;
        if (!srval2str.empty()) return false;
    } else {
        // We will need to use a twotag cf as the index
        if (sname.empty()) {
            // Where Query did not specify a suffix. Insert a NULL suffix
            sname = *(cdesc.suffixes.begin());

            // The suffix attribute MUST exist in the schema
            StatsQuery::column_t cdesc2;
            if (m_query->stats().is_stat_table_static()) {
                cdesc2 = m_query->stats().get_column_desc(sname);
            } else {
                cdesc2 = get_column_desc(table_schema, sname);;
            }
                
            if (cdesc2.datatype == QEOpServerProxy::STRING) {
                sval = std::string("");
            } else if (cdesc2.datatype == QEOpServerProxy::UINT64){
                sval = (uint64_t) 0;
            } else {
                QE_ASSERT(0);
            }
            QE_TRACE(DEBUG, "StatTable Where Suffix creation of " << sname);
        } else {
            // Where query specified a suffix. Check that it is valid
            if (cdesc.suffixes.find(sname)==cdesc.suffixes.end()) return false;

            // The suffix attribute MUST exist in the schema
            StatsQuery::column_t cdesc2;
            if (m_query->stats().is_stat_table_static()) {
                cdesc2 = m_query->stats().get_column_desc(sname);
            } else {
                cdesc2 = get_column_desc(table_schema, sname);;
            }
            QE_ASSERT ((cdesc2.datatype == QEOpServerProxy::STRING) ||
            (cdesc2.datatype == QEOpServerProxy::UINT64));

            // Now fill in the suffix value and value2 based on types in schema
            sval = ToDbDataValue(srvalstr, cdesc2.datatype);
            if (!srval2str.empty()) {
                sval2 = ToDbDataValue(srval2str, cdesc2.datatype);
            }
            QE_TRACE(DEBUG, "StatTable Where Suffix match of " << cdesc2.datatype);
        }
    }

    return true;
}

static bool StatSlicer(DbQueryUnit *db_query, match_op op,
        const GenDb::DbDataValue& val, const GenDb::DbDataValue& val2) {
    if (val.which() == GenDb::DB_VALUE_STRING) {
        if (!((op == EQUAL) || (op == PREFIX))) return false;
    } else {
        if (!((op == EQUAL) || (op == IN_RANGE))) return false;
    }
    db_query->cr.start_.push_back(val);
    if (op == PREFIX) {
        std::string str_smpl2(boost::get<std::string>(val) + "\x7f");
        db_query->cr.finish_.push_back(str_smpl2);
    } else if (op == IN_RANGE) {
        db_query->cr.finish_.push_back(val2);
    } else {
        db_query->cr.finish_.push_back(val);
    }
    return true;
}

bool WhereQuery::StatTermProcess(const contrail_rapidjson::Value& where_term,
        QueryUnit* and_node, QueryUnit *main_query) {

    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    std::string pname,sname,cfname;
    match_op pop,sop;
    GenDb::DbDataValue pval, pval2, sval, sval2;

    bool res = StatTermParse(main_query, where_term,
            pname, pop, pval, pval2, sname, sop, sval, sval2);

    if (!res) return false;
    
    bool twotag = true;
    if ((sop==(match_op)0)&&(sname.empty())) {
        // We need to look at the single-tag stat index tables
        twotag = false;
        if (pval.which() == GenDb::DB_VALUE_STRING) {
            cfname = g_viz_constants.STATS_TABLE_BY_STR_TAG;
        } else if (pval.which() == GenDb::DB_VALUE_UINT64) {
            cfname = g_viz_constants.STATS_TABLE_BY_U64_TAG;
        } else if (pval.which() == GenDb::DB_VALUE_DOUBLE) {
            cfname = g_viz_constants.STATS_TABLE_BY_DBL_TAG;
        } else {
            QE_TRACE(DEBUG, "For single-tag index table, wrong WHERE type " <<
                    pval.which());
            return false;
        }
    } else {
        if (pval.which() == GenDb::DB_VALUE_STRING) {
            if (sval.which() == GenDb::DB_VALUE_STRING) {
                cfname = g_viz_constants.STATS_TABLE_BY_STR_STR_TAG;
            } else if (sval.which() == GenDb::DB_VALUE_UINT64) {
                cfname = g_viz_constants.STATS_TABLE_BY_STR_U64_TAG;
            } else {
                QE_TRACE(DEBUG, "For two-tag STR table, wrong WHERE suffix type " <<
                        sval.which());
                return false;
            }
        } else if (pval.which() == GenDb::DB_VALUE_UINT64) {
            if (sval.which() == GenDb::DB_VALUE_STRING) {
                cfname = g_viz_constants.STATS_TABLE_BY_U64_STR_TAG;
            } else if (sval.which() == GenDb::DB_VALUE_UINT64) {
                cfname = g_viz_constants.STATS_TABLE_BY_U64_U64_TAG;
            } else {
                QE_TRACE(DEBUG, "For two-tag U64 table, wrong WHERE suffix type " <<
                        sval.which());
                return false;
            }
        } else {
            QE_TRACE(DEBUG, "For two-tag index table, wrong WHERE prefix type " <<
                    pval.which());
            return false;
        }
    }
    QE_TRACE(DEBUG, "Query Stat Index " << cfname <<  " twotag " << twotag);
    DbQueryUnit *db_query = new DbQueryUnit(and_node, main_query);

    db_query->t_only_col = false;
    db_query->t_only_row = false;
    db_query->cfname = cfname;

    size_t tpos,apos;
    std::string tname = m_query->table();
    tpos = tname.find('.');
    apos = tname.find('.', tpos+1);

    std::string tstr = tname.substr(tpos+1, apos-tpos-1);
    std::string astr = tname.substr(apos+1, std::string::npos);

    db_query->row_key_suffix.push_back(tstr);
    db_query->row_key_suffix.push_back(astr);
    db_query->row_key_suffix.push_back(pname);
  
    if (twotag) {
        db_query->row_key_suffix.push_back(sname);
        if (sop==(match_op)0) {
            // We will only be using the prefix value for querying
            if (!StatSlicer(db_query, pop, pval, pval2)) return false;

            if (sval.which() == GenDb::DB_VALUE_STRING) {
                db_query->cr.start_.push_back(std::string("\x00"));
                db_query->cr.finish_.push_back(std::string("\x7f"));
            } else {
                db_query->cr.start_.push_back((uint64_t)0);
                db_query->cr.finish_.push_back((uint64_t)0xffffffffffffffff);
            }
        } else {
            // We will be using the suffix value for querying
            if (!(pop == EQUAL)) return false;
            db_query->cr.start_.push_back(pval);
            db_query->cr.finish_.push_back(pval);

            if (!StatSlicer(db_query, sop, sval, sval2)) return false;
        }

    } else {
        if (!StatSlicer(db_query, pop, pval, pval2)) return false;
    }

    return true;
}

bool SessionTableQueryColumn2CassColumn(const std::string& name, std::string *cass_name) {
    std::map<string, table_schema>::const_iterator it =
        (g_viz_constants._VIZD_SESSION_TABLE_SCHEMA.find(
            g_viz_constants.SESSION_TABLE));
    QE_ASSERT(it != g_viz_constants._VIZD_SESSION_TABLE_SCHEMA.end());
    std::map<string, string>::const_iterator itr =
        it->second.index_column_to_column.find(name);
    if (itr == (it->second.index_column_to_column.end())) {
        return false;
    }
    *cass_name = itr->second;
    return true; 
}

WhereQuery::WhereQuery(const std::string& where_json_string, int session_type,
        int is_si, int direction, int32_t or_number, QueryUnit *main_query):
    QueryUnit(main_query, main_query), direction_ing(direction),
    json_string_(where_json_string), wterms_(0) {
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    where_result_.reset(new std::vector<query_result_unit_t>);
    if (where_json_string == std::string(""))
    {
        if (or_number == -1) wterms_ = 1;
        DbQueryUnit *db_query = new DbQueryUnit(this, main_query);

        //TBD not sure if this will work for Message table or Object Log
        if (m_query->is_message_table_query()) {
            db_query->cfname = g_viz_constants.COLLECTOR_GLOBAL_TABLE;
            db_query->t_only_col = true;
            db_query->t_only_row = true;
        } else if 
        ((m_query->table() == g_viz_constants.FLOW_TABLE)
        || (m_query->table() == g_viz_constants.FLOW_SERIES_TABLE)) {
#ifdef USE_SESSION
            DbQueryUnit *db_query_client = new DbQueryUnit(this, main_query);
            {
                db_query->cfname = g_viz_constants.SESSION_TABLE;
                db_query->row_key_suffix.push_back((uint8_t)is_si);
                db_query->row_key_suffix.push_back(
                                (uint8_t)SessionType::SERVER_SESSION);
                // starting value for clustering key range
                db_query->cr.start_.push_back((uint16_t)0);

                // ending value for clustering key range
                db_query->cr.finish_.push_back((uint16_t)0xffff);
                db_query->cr.finish_.push_back((uint16_t)0xffff);
            }
            {
                db_query_client->cfname = g_viz_constants.SESSION_TABLE;
                db_query_client->row_key_suffix.push_back((uint8_t)is_si);
                db_query_client->row_key_suffix.push_back(
                                (uint8_t)SessionType::CLIENT_SESSION);
                // starting value for clustering key range
                db_query_client->cr.start_.push_back((uint16_t)0);

                // ending value for clustering key range
                db_query_client->cr.finish_.push_back((uint16_t)0xffff);
                db_query_client->cr.finish_.push_back((uint16_t)0xffff);

            }
#else
            db_query->row_key_suffix.push_back((uint8_t)direction_ing);
            db_query->cfname = g_viz_constants.FLOW_TABLE_PROT_SP;

            // starting value for protocol/port field
            db_query->cr.start_.push_back((uint8_t)0);

            // ending value for protocol/port field;
            db_query->cr.finish_.push_back((uint8_t)0xff);
            db_query->cr.finish_.push_back((uint16_t)0xffff);
#endif
        } else if (m_query->is_session_query(m_query->table())) {

            db_query->row_key_suffix.push_back((uint8_t)is_si);
            db_query->row_key_suffix.push_back((uint8_t)session_type);
            db_query->cfname = g_viz_constants.SESSION_TABLE;

            // starting value for clustering key range
            db_query->cr.start_.push_back((uint16_t)0);

            // ending value for clustering key range
            db_query->cr.finish_.push_back((uint16_t)0xffff);
            db_query->cr.finish_.push_back((uint16_t)0xffff);

        } else if (m_query->is_object_table_query(m_query->table())) {
            db_query->cfname = g_viz_constants.COLLECTOR_GLOBAL_TABLE;
            db_query->t_only_col = true;
            db_query->t_only_row = true;
            bool object_id_specified = false;

            // handling where * for object table is similar to 
            // and subset of object-id=X handling
            handle_object_type_value(m_query, db_query, object_id_specified);
            QE_TRACE(DEBUG, "where * for object table" << m_query->table());

        }
        // This is "where *" query, no need to do JSON parsing
        return;
    }

    // Do JSON parsing
    contrail_rapidjson::Document d;
    std::string json_string = "{ \"where\" : " + 
        where_json_string + " }";

    QE_TRACE(DEBUG, "where query:" << json_string);
    d.Parse<0>(const_cast<char *>(json_string.c_str()));
    const contrail_rapidjson::Value& json_or_list = d["where"]; 
    QE_PARSE_ERROR(json_or_list.IsArray());

    QE_TRACE(DEBUG, "number of OR terms in where :" << json_or_list.Size());

    if (or_number == -1) wterms_ = json_or_list.Size();

    for (contrail_rapidjson::SizeType i = 0; i < json_or_list.Size(); i++) 
    {
        const contrail_rapidjson::Value& json_or_node = json_or_list[i];
        QE_PARSE_ERROR(json_or_list[i].IsArray());
        QE_INVALIDARG_ERROR(json_or_list[i].Size() != 0);

        // If the or_number is -1, we are in query prepare.
        // We have no intention of actually executing the query.
        // But, we parse everything to catch errors.
        if (or_number != -1) {
            // Only execute the requested OR term
            if (or_number != (int)i) continue;
        }

        QE_TRACE(DEBUG, "number of AND term in " << (i+1) << 
                "th OR term is " <<json_or_node.Size());

        // these are needed because flow index table queries
        // span multiple WHERE match component
        bool vr_match = false; GenDb::DbDataValue vr, vr2; int vr_op = 0;
        bool svn_match = false; GenDb::DbDataValue svn, svn2; int svn_op = 0;
        bool dvn_match = false; GenDb::DbDataValue dvn, dvn2; int dvn_op = 0;
        bool sip_match = false; GenDb::DbDataValue sip, sip2; int sip_op = 0;
        bool dip_match = false; GenDb::DbDataValue dip, dip2; int dip_op = 0;
        bool proto_match = false; GenDb::DbDataValue proto, proto2; int proto_op = 0;
        bool sport_match = false; GenDb::DbDataValue sport, sport2; int sport_op = 0;
        bool dport_match = false; GenDb::DbDataValue dport, dport2; int dport_op = 0;
        bool object_id_specified = false;
        bool isSession = m_query->is_session_query(m_query->table());
        GenDb::WhereIndexInfoVec where_vec;
        std::vector<filter_match_t> filter_and;

        // All where parameters in subquery are AND.
        // So they are in the same msg_table_db_query object.
        // If there are no where-params, this would result in no-op.
        DbQueryUnit *msg_table_db_query = NULL;
        if (m_query->is_message_table_query() ||
            m_query->is_object_table_query(m_query->table())) {

            msg_table_db_query = new DbQueryUnit(this, main_query);
            msg_table_db_query->cfname = g_viz_constants.COLLECTOR_GLOBAL_TABLE;
            msg_table_db_query->t_only_row = true;
            msg_table_db_query->t_only_col = true;
        }

        for (contrail_rapidjson::SizeType j = 0; j < json_or_node.Size(); j++)
        {
            QE_PARSE_ERROR((json_or_node[j].HasMember(WHERE_MATCH_NAME) &&
                json_or_node[j].HasMember(WHERE_MATCH_VALUE) &&
                json_or_node[j].HasMember(WHERE_MATCH_OP)));
            const contrail_rapidjson::Value& name_value =
                json_or_node[j][WHERE_MATCH_NAME];
            const contrail_rapidjson::Value&  value_value =
                json_or_node[j][WHERE_MATCH_VALUE];
            const contrail_rapidjson::Value& op_value =
                json_or_node[j][WHERE_MATCH_OP];

            // do some validation checks
            QE_INVALIDARG_ERROR(name_value.IsString());
            QE_INVALIDARG_ERROR
                ((value_value.IsString() || value_value.IsNumber()));
            QE_INVALIDARG_ERROR(op_value.IsNumber());

            std::string name = name_value.GetString();
            QE_INVALIDARG_ERROR(m_query->is_valid_where_field(name));

            // extract value after type conversion
            std::string value;
            {
                if (value_value.IsString())
                {
                    value = value_value.GetString();
                } else if (value_value.IsInt()){
                    int int_value;
                    std::ostringstream convert;
                    int_value = value_value.GetInt();
                    convert << int_value;
                    value = convert.str();
                } else if (value_value.IsUint()) {
                    uint32_t uint_value;
                    std::ostringstream convert;
                    uint_value = value_value.GetUint();
                    convert << uint_value;
                    value = convert.str();
                } else if (value_value.IsDouble()) {
                    double dbl_value;
                    std::ostringstream convert;
                    dbl_value = value_value.GetDouble();
                    convert << dbl_value;
                    value = convert.str();
                }
            }

            match_op op = (match_op)op_value.GetInt();

            name = get_column_name(name); // Get actual Cassandra name
           
            // this is for range queries
            std::string value2;
            if (op == IN_RANGE)
            {
                QE_PARSE_ERROR(json_or_node[j].HasMember(WHERE_MATCH_VALUE2));
                const contrail_rapidjson::Value&  value_value2 =
                json_or_node[j][WHERE_MATCH_VALUE2];

                // extract value2 after type conversion
                if (value_value2.IsString())
                {
                    value2 = value_value2.GetString();
                } else if (value_value2.IsInt()){
                    int int_value;
                    std::ostringstream convert;
                    int_value = value_value2.GetInt();
                    convert << int_value;
                    value2 = convert.str();
                } else if (value_value2.IsUint()) {
                    uint32_t uint_value;
                    std::ostringstream convert;
                    uint_value = value_value2.GetUint();
                    convert << uint_value;
                    value2 = convert.str();
                } else if (value_value2.IsDouble()) {
                    double dbl_value;
                    std::ostringstream convert;
                    dbl_value = value_value2.GetDouble();
                    convert << dbl_value;
                    value2 = convert.str();
                }
            }

            bool isStat = m_query->is_stat_table_query(m_query->table());
            if ((name == g_viz_constants.SOURCE) && (!isStat))
            {
                QE_INVALIDARG_ERROR((op == EQUAL) || (op == PREFIX));
                populate_where_vec(msg_table_db_query, name, op,
                                   std::string(""), value);
                QE_TRACE(DEBUG, "where match term for source " << value);
            }


            if ((name == g_viz_constants.MODULE) && (!isStat))
            {
                QE_INVALIDARG_ERROR((op == EQUAL) || (op == PREFIX));
                populate_where_vec(msg_table_db_query, name, op,
                                   std::string(""), value);

                // dont filter query engine logs if the query is about query
                // engine
                if (value == m_query->sandesh_moduleid)
                    m_query->filter_qe_logs = false;

                QE_TRACE(DEBUG, "where match term for module " << value);
            }

            if ((name == g_viz_constants.MESSAGE_TYPE) && (!isStat))
            {
                QE_INVALIDARG_ERROR((op == EQUAL) || (op == PREFIX));
                populate_where_vec(msg_table_db_query, name, op,
                                   std::string(""), value);
                QE_TRACE(DEBUG, "where match term for msg-type " << value);
            }

            if (name == OBJECTID)
            {
                QE_INVALIDARG_ERROR((op == EQUAL) || (op == PREFIX));

                // Object-id is saved in column[6..11] in MessageTablev2 in the format
                // T2:ObjectType:ObjectId
                // T2: is prefixed later, we need to prefix ObjectType: here.
                std::string value_prefix = m_query->table();
                value_prefix.append(":");
                std::string col_name = g_viz_constants.OBJECT_TYPE_NAME1;
                populate_where_vec(msg_table_db_query, col_name, op,
                                   value_prefix, value);
                object_id_specified = true;
                QE_TRACE(DEBUG, "where match term for objectid " << value);
            }

            if (m_query->is_session_query(m_query->table())) {
                if (name == g_viz_constants.SessionRecordNames[
                                SessionRecordFields::SESSION_PROTOCOL])
                {
                    proto_match = true; proto_op = op;
                    uint16_t proto_value, proto_value2;
                    std::istringstream(value) >> proto_value;
                    proto = proto_value;
                    if (proto_op == IN_RANGE)
                    {
                        std::istringstream(value2) >> proto_value2;
                        proto2 = proto_value2;
                    } else {
                        QE_INVALIDARG_ERROR(proto_op == EQUAL);
                    }
                    QE_TRACE(DEBUG, "where match term for proto_value " << value);
                }
                else if (name == g_viz_constants.SessionRecordNames[
                                SessionRecordFields::SESSION_SPORT])
                {
                    sport_match = true; sport_op = op;
                    uint16_t sport_value, sport_value2;
                    std::istringstream(value) >> sport_value;
                    sport = sport_value;
                    if (sport_op == IN_RANGE)
                    {
                        std::istringstream(value2) >> sport_value2;
                        sport2 = sport_value2;
                    } else {
                        QE_INVALIDARG_ERROR(sport_op == EQUAL);
                    }
                    QE_TRACE(DEBUG, "where match term for sport_value " << value);
                }
                else {
                    std::string columnN;
                    if (!SessionTableQueryColumn2CassColumn(name, &columnN)) {
                        QE_INVALIDARG_ERROR(0);
                    }
                    GenDb::Op::type comparator;
                    if (op == PREFIX) {
                        if (value == "") {
                            continue;
                        }
                        value += "%";
                        comparator = GenDb::Op::LIKE;
                    } else {
                        comparator = GenDb::Op::EQ;
                    }
                    GenDb::WhereIndexInfo where_info = boost::make_tuple(columnN,
                            comparator, value);
                    where_vec.push_back(where_info);
                }
#ifdef USE_SESSION
            } else if (m_query->is_flow_query(m_query->table())){
                if (name == g_viz_constants.FlowRecordNames[
                                FlowRecordFields::FLOWREC_PROTOCOL])
                {
                    proto_match = true; proto_op = op;
                    uint16_t proto_value, proto_value2;
                    std::istringstream(value) >> proto_value;
                    proto = proto_value;
                    if (proto_op == IN_RANGE)
                    {
                        std::istringstream(value2) >> proto_value2;
                        proto2 = proto_value2;
                    } else {
                        QE_INVALIDARG_ERROR(proto_op == EQUAL);
                    }
                    QE_TRACE(DEBUG, "where match term for proto_value " << value);
                }
                if (name == g_viz_constants.FlowRecordNames[
                                FlowRecordFields::FLOWREC_SOURCEVN])
                {
                    svn_match = true; svn_op = op;
                    svn = value;
                    QE_INVALIDARG_ERROR((svn_op == EQUAL)||(svn_op == PREFIX));

                    QE_TRACE(DEBUG, "where match term for sourcevn " << value);
                }
                if (name == g_viz_constants.FlowRecordNames[
                                FlowRecordFields::FLOWREC_DESTVN])
                {
                    dvn_match = true; dvn_op = op;
                    dvn = value;
                    QE_INVALIDARG_ERROR((dvn_op == EQUAL)||(dvn_op == PREFIX));

                    QE_TRACE(DEBUG, "where match term for sourcevn " << value);
                }
                if (name == g_viz_constants.FlowRecordNames[
                                FlowRecordFields::FLOWREC_SOURCEIP])
                {
                    sip_match = true; sip_op = op;
                    sip = value;
                    QE_TRACE(DEBUG, "where match term for sourceip " << value);
                    if (sip_op == IN_RANGE)
                    {
                        sip2 = value2;
                    } else {
                        QE_INVALIDARG_ERROR(sip_op == EQUAL);
                    }
                    if (direction_ing == 0) {
                        filter_match_t filter;
                        filter.name = "sourceip";
                        filter.op = (match_op)sip_op;
                        filter.value = boost::get<std::string>(sip);
                        filter_and.push_back(filter);
                        additional_select_.push_back(filter.name);
                    }
                }
                if (name == g_viz_constants.FlowRecordNames[
                                FlowRecordFields::FLOWREC_DESTIP])
                {
                    dip_match = true; dip_op = op;
                    dip = value;
                    QE_TRACE(DEBUG, "where match term for destip " << value);
                    if (dip_op == IN_RANGE)
                    {
                        dip2 = value2;
                    } else {
                        QE_INVALIDARG_ERROR(dip_op == EQUAL);
                    }
                    if (direction_ing == 1) {
                        filter_match_t filter;
                        filter.name = "destip";
                        filter.op = (match_op)dip_op;
                        filter.value = boost::get<std::string>(dip);
                        filter_and.push_back(filter);
                        additional_select_.push_back(filter.name);
                    }
                }
                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SPORT])
                {
                    sport_match = true; sport_op = op;

                    uint16_t sport_value;
                    std::istringstream(value) >> sport_value;

                    sport = sport_value;
                    if (sport_op == IN_RANGE)
                    {
                        uint16_t sport_value2;
                        std::istringstream(value2) >> sport_value2;
                        sport2 = sport_value2;
                    } else {
                        QE_INVALIDARG_ERROR(sport_op == EQUAL);
                    }

                    filter_match_t filter;
                    filter.name = "sport";
                    filter.op = (match_op)sport_op;
                    std::ostringstream convert;
                    convert << boost::get<uint16_t>(sport);
                    filter.value = convert.str();
                    filter_and.push_back(filter);
                    additional_select_.push_back(filter.name);

                    QE_TRACE(DEBUG, "where match term for sport " << value);
                }
                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DPORT])
                {
                    dport_match = true; dport_op = op;

                    uint16_t dport_value;
                    std::istringstream(value) >> dport_value;
                    dport = dport_value;
                    if (dport_op == IN_RANGE)
                    {
                        uint16_t dport_value2;
                        std::istringstream(value2) >> dport_value2;
                        dport2 = dport_value2;
                    } else {
                        QE_INVALIDARG_ERROR(dport_op == EQUAL);
                    }

                    filter_match_t filter;
                    filter.name = "dport";
                    filter.op = (match_op)dport_op;
                    std::ostringstream convert;
                    convert << boost::get<uint16_t>(dport);
                    filter.value = convert.str();
                    filter_and.push_back(filter);
                    additional_select_.push_back(filter.name);

                    QE_TRACE(DEBUG, "where match term for dport " << value);
                }
                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VROUTER])
                {
                    vr_match = true;
                    vr_op = op;
                    vr = value;
                    QE_INVALIDARG_ERROR((vr_op == EQUAL)||(vr_op == PREFIX));

                    QE_TRACE(DEBUG, "where match term for vrouter " << value);
                    filter_match_t filter;
                    filter.name = "vrouter";
                    if (vr_op != PREFIX) {
                        filter.op = (match_op)vr_op;
                    } else {
                        filter.op = REGEX_MATCH;
                    }
                    filter.value = boost::get<std::string>(vr);
                    if (filter.op == REGEX_MATCH) {
                        filter.match_e = regex(filter.value);
                    }
                    if (vr_match) {
                    }
                    filter_and.push_back(filter);
                    additional_select_.push_back(filter.name);
                }
            }
#else
            else if (m_query->is_flow_query(m_query->table())) {
                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VROUTER])
                {
                    vr_match = true; vr_op = op;
                    vr = value;
                    if (vr_op == PREFIX)
                    {
                        vr2 = value + "\x7f";
                    }
                    QE_INVALIDARG_ERROR((vr_op == EQUAL)||(vr_op == PREFIX));

                    QE_TRACE(DEBUG, "where match term for vrouter " << value);
                }
                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEVN])
                {
                    svn_match = true; svn_op = op;
                    svn = value;
                    if (svn_op == PREFIX)
                    {
                        svn2 = value + "\x7f";
                    }
                    QE_INVALIDARG_ERROR((svn_op == EQUAL)||(svn_op == PREFIX));

                    QE_TRACE(DEBUG, "where match term for sourcevn " << value);
                }

                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEIP])
                {
                    sip_match = true; sip_op = op;
                    boost::system::error_code ec;
                    sip = IpAddress::from_string(value, ec);
                    QE_INVALIDARG_ERROR(ec == 0);
                    QE_TRACE(DEBUG, "where match term for sourceip " << value);
                    if (sip_op == IN_RANGE)
                    {
                        boost::system::error_code ec;
                        sip2 = IpAddress::from_string(value2, ec);
                        QE_INVALIDARG_ERROR(ec == 0);
                    } else {
                        QE_INVALIDARG_ERROR(sip_op == EQUAL);
                    }
                }

                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTVN])
                {
                    dvn_match = true; dvn_op = op;
                    dvn = value;
                    if (dvn_op == PREFIX)
                    {
                        dvn2 = value + "\x7f";
                    }

                    QE_INVALIDARG_ERROR((dvn_op == EQUAL)||(dvn_op == PREFIX));

                    QE_TRACE(DEBUG, "where match term for destvn " << value);
                }

                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTIP])
                {
                    dip_match = true; dip_op = op; dip = value;
                    boost::system::error_code ec;
                    dip = IpAddress::from_string(value, ec);
                    QE_INVALIDARG_ERROR(ec == 0);
                    QE_TRACE(DEBUG, "where match term for destip " << value);
                    if (dip_op == IN_RANGE)
                    {
                        boost::system::error_code ec;
                        dip2 = IpAddress::from_string(value2, ec);
                        QE_INVALIDARG_ERROR(ec == 0);
                    } else {
                        QE_INVALIDARG_ERROR(dip_op == EQUAL);
                    }

                }

                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_PROTOCOL])
                {
                    proto_match = true; proto_op = op;

                    uint16_t proto_value;
                    std::istringstream(value) >> proto_value;
                    proto = (uint8_t)proto_value;
                    if (proto_op == IN_RANGE)
                    {
                        uint16_t proto_value2;
                        std::istringstream(value2) >> proto_value2;
                        proto2 = (uint8_t)proto_value2;
                    } else {
                        QE_INVALIDARG_ERROR(proto_op == EQUAL);
                    }

                    QE_TRACE(DEBUG, "where match term for proto_value " << value);
                }

                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SPORT])
                {
                    sport_match = true; sport_op = op;

                    uint16_t sport_value;
                    std::istringstream(value) >> sport_value;

                    sport = sport_value;
                    if (sport_op == IN_RANGE)
                    {
                        uint16_t sport_value2;
                        std::istringstream(value2) >> sport_value2;
                        sport2 = sport_value2;
                    } else {
                        QE_INVALIDARG_ERROR(sport_op == EQUAL);
                    }

                    QE_TRACE(DEBUG, "where match term for sport " << value);
                }

                if (name == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DPORT])
                {
                    dport_match = true; dport_op = op;

                    uint16_t dport_value;
                    std::istringstream(value) >> dport_value;
                    dport = dport_value;
                    if (dport_op == IN_RANGE)
                    {
                        uint16_t dport_value2;
                        std::istringstream(value2) >> dport_value2;
                        dport2 = dport_value2;
                    } else {
                        QE_INVALIDARG_ERROR(dport_op == EQUAL);
                    }

                    QE_TRACE(DEBUG, "where match term for dport " << value);
                }
            }
#endif
            if (isStat)
            {
                StatTermProcess(json_or_node[j], this, main_query);
                object_id_specified = true;
            }
        }

        // common handling similar to object table where * case
        if (m_query->is_message_table_query() ||
            m_query->is_object_table_query(m_query->table())) {
            handle_object_type_value(m_query, msg_table_db_query,
                                     object_id_specified);
        }

#ifndef USE_SESSION
        if (!isSession) {
#else
        if (!isSession && !m_query->is_flow_query(m_query->table())) {
#endif
            // do some validation checks
            if (sip_match && !(svn_match))
            {
                // SIP specified without SVN
                QE_INVALIDARG_ERROR(0);
            }

            if (dip_match && !(dvn_match))
            {
                // DIP specified without DVN
                QE_INVALIDARG_ERROR(0);
            }

            if ((sport_match || dport_match) && !(proto_match))
            {
                // ports specified without protocol
                QE_INVALIDARG_ERROR(0);
            }

            if ((svn_op != EQUAL) && (sip_match))
            {
                // can not do range query on svn when sip is specified
                QE_INVALIDARG_ERROR(0);
            }

            if ((dvn_op != EQUAL) && (dip_match))
            {
                // can not do range query on dvn when dip is specified
                QE_INVALIDARG_ERROR(0);
            }

            if ((proto_op != EQUAL) && ((sport_match) || (dport_match)))
            {
                // can not do range query on protocol with dport or sport
                QE_INVALIDARG_ERROR(0);
            }
        }
        if (isSession) {

            DbQueryUnit *session_db_query = new DbQueryUnit(this, main_query);
            session_db_query->cfname = g_viz_constants.SESSION_TABLE;
            session_db_query->row_key_suffix.push_back((uint8_t)is_si);
            session_db_query->row_key_suffix.push_back((uint8_t)session_type);
            session_db_query->where_vec = where_vec;

            if (proto_match) {
                session_db_query->cr.start_.push_back(proto);
                if (proto_op == EQUAL) {
                    session_db_query->cr.finish_.push_back(proto);
                } else if (proto_op == IN_RANGE) {
                    session_db_query->cr.finish_.push_back(proto2);
                }
            } else {
                session_db_query->cr.start_.push_back((uint16_t)0);
                session_db_query->cr.finish_.push_back((uint16_t)0xffff);
            }
            if (sport_match) {
                QE_INVALIDARG_ERROR(proto_match);
                session_db_query->cr.start_.push_back(sport);
                if(sport_op == EQUAL) {
                    session_db_query->cr.finish_.push_back(sport);
                } else if (sport_op == IN_RANGE) {
                    session_db_query->cr.finish_.push_back(sport2);
                }
            } else {
                session_db_query->cr.finish_.push_back((uint16_t)0xffff);
            }
        }
#ifdef USE_SESSION
        else if (m_query->is_flow_query(m_query->table())) {
            if (!filter_and.empty()) {
                filter_list_.push_back(filter_and);
            }
            {
                DbQueryUnit *client_session_query = new DbQueryUnit(this, main_query);
                client_session_query->cfname = g_viz_constants.SESSION_TABLE;
                client_session_query->row_key_suffix.push_back(
                                        (uint8_t)SessionType::CLIENT_SESSION);
                if (proto_match) {
                    client_session_query->cr.start_.push_back(proto);
                    if (proto_op == EQUAL) {
                        client_session_query->cr.finish_.push_back(proto);
                    } else if (proto_op == IN_RANGE) {
                        client_session_query->cr.finish_.push_back(proto2);
                    }
                } else {
                    client_session_query->cr.start_.push_back(((uint16_t)0));
                    client_session_query->cr.finish_.push_back(((uint16_t)0xffff));
                }
                if ((direction_ing == 0 && sport_match) ||
                    (direction_ing == 1 && dport_match)) {
                    QE_INVALIDARG_ERROR(proto_match);
                    client_session_query->cr.start_.push_back(direction_ing?
                            dport:sport);
                    int op = direction_ing?dport_op:sport_op;
                    if (op == EQUAL) {
                        client_session_query->cr.finish_.push_back(direction_ing?
                            dport:sport);
                    } else if (op == IN_RANGE) {
                        client_session_query->cr.finish_.push_back(direction_ing?
                            dport2:sport2);
                    }
                } else {
                    client_session_query->cr.finish_.push_back((uint16_t)0xffff);
                }
                if ((direction_ing == 0 && dip_match) ||
                    (direction_ing == 1 && sip_match)) {
                    std::string columnN;
                    if (!SessionTableQueryColumn2CassColumn("local_ip", &columnN)) {
                        QE_INVALIDARG_ERROR(0);
                    }

                    int op = direction_ing?sip_op:dip_op;
                    std::string val = direction_ing?
                        (boost::get<std::string>(sip)):(boost::get<std::string>(dip));
                    GenDb::Op::type comparator;
                    if (op == PREFIX) {
                        if (val == "") {
                            continue;
                        }
                        val += "%";
                        comparator = GenDb::Op::LIKE;
                    } else {
                        comparator = GenDb::Op::EQ;
                    }
                    GenDb::WhereIndexInfo where_info = boost::make_tuple(columnN,
                        comparator, val);
                    client_session_query->where_vec.push_back(where_info);
                }
                if ((direction_ing == 0 && dvn_match) ||
                    (direction_ing == 1 && svn_match)) {
                    std::string columnN;
                    if (!SessionTableQueryColumn2CassColumn("vn", &columnN)) {
                        QE_INVALIDARG_ERROR(0);
                    }
                    int op = (direction_ing?svn_op:dvn_op);
                    std::string val = direction_ing?
                        (boost::get<std::string>(svn)):(boost::get<std::string>(dvn));
                    GenDb::Op::type comparator;
                    if (op == PREFIX) {
                        if (val == "") {
                            continue;
                        }
                        val += "%";
                        comparator = GenDb::Op::LIKE;
                    } else {
                        comparator = GenDb::Op::EQ;
                    }
                    GenDb::WhereIndexInfo where_info = boost::make_tuple(columnN,
                        comparator, val);
                    client_session_query->where_vec.push_back(where_info);
                }
                if ((direction_ing == 0 && svn_match) ||
                    (direction_ing == 1 && dvn_match)) {
                    std::string columnN;
                    if (!SessionTableQueryColumn2CassColumn("remote_vn", &columnN)) {
                        QE_INVALIDARG_ERROR(0);
                    }
                    int op = (direction_ing?dvn_op:svn_op);
                    GenDb::Op::type comparator;
                    std::string val = direction_ing?
                        (boost::get<std::string>(dvn)):(boost::get<std::string>(svn));
                    if (op == PREFIX) {
                        if (val == "") {
                            continue;
                        }
                        val += "%";
                        comparator = GenDb::Op::LIKE;
                    } else {
                        comparator = GenDb::Op::EQ;
                    }
                    GenDb::WhereIndexInfo where_info = boost::make_tuple(columnN,
                        comparator, val);
                    client_session_query->where_vec.push_back(where_info);
                }
            }
            {
                DbQueryUnit *server_session_query = new DbQueryUnit(this, main_query);
                server_session_query->cfname = g_viz_constants.SESSION_TABLE;
                server_session_query->row_key_suffix.push_back(
                                        (uint8_t)SessionType::SERVER_SESSION);
                if (proto_match) {
                    server_session_query->cr.start_.push_back(proto);
                    if(proto_op == EQUAL) {
                        server_session_query->cr.finish_.push_back(proto);
                    }
                    else if (proto_op == IN_RANGE) {
                        server_session_query->cr.finish_.push_back(proto2);
                    }
                } else {
                    server_session_query->cr.start_.push_back(((uint16_t)0));
                    server_session_query->cr.finish_.push_back(((uint16_t)0xffff));
                }
                if ((direction_ing == 0 && dport_match) ||
                    (direction_ing == 1 && sport_match)) {
                    QE_INVALIDARG_ERROR(proto_match);
                    server_session_query->cr.start_.push_back(direction_ing?
                            sport:dport);
                    int op = direction_ing?sport_op:dport_op;
                    if(op == EQUAL) {
                        server_session_query->cr.finish_.push_back(direction_ing?
                            sport:dport);
                    } else if (op == IN_RANGE) {
                        server_session_query->cr.finish_.push_back(direction_ing?
                            sport2:dport2);
                    }
                } else {
                    server_session_query->cr.finish_.push_back((uint16_t)0xffff);
                }
                if ((direction_ing == 0 && dip_match) ||
                    (direction_ing == 1 && sip_match)) {
                    std::string columnN;
                    if (!SessionTableQueryColumn2CassColumn("local_ip", &columnN)) {
                        QE_INVALIDARG_ERROR(0);
                    }
                    int op = direction_ing?sip_op:dip_op;
                    std::string val = direction_ing?
                        (boost::get<std::string>(sip)):(boost::get<std::string>(dip));
                    GenDb::Op::type comparator;
                    if (op == PREFIX) {
                        if (val == "") {
                            continue;
                        }
                        val += "%";
                        comparator = GenDb::Op::LIKE;
                    } else {
                        comparator = GenDb::Op::EQ;
                    }
                    GenDb::WhereIndexInfo where_info = boost::make_tuple(columnN,
                        comparator, val);
                    server_session_query->where_vec.push_back(where_info);
                }
                if ((direction_ing == 0 && dvn_match) ||
                    (direction_ing == 1 && svn_match)) {
                    std::string columnN;
                    if (!SessionTableQueryColumn2CassColumn("vn", &columnN)) {
                        QE_INVALIDARG_ERROR(0);
                    }
                    int op = (direction_ing?svn_op:dvn_op);
                    std::string val = direction_ing?
                        (boost::get<std::string>(svn)):(boost::get<std::string>(dvn));
                    GenDb::Op::type comparator;
                    if (op == PREFIX) {
                        if (val == "") {
                            continue;
                        }
                        val += "%";
                        comparator = GenDb::Op::LIKE;
                    } else {
                        comparator = GenDb::Op::EQ;
                    }
                    GenDb::WhereIndexInfo where_info = boost::make_tuple(columnN,
                        comparator, val);
                    server_session_query->where_vec.push_back(where_info);
                }
                if ((direction_ing == 0 && svn_match) ||
                    (direction_ing == 1 && dvn_match)) {
                    std::string columnN;
                    if (!SessionTableQueryColumn2CassColumn("remote_vn", &columnN)) {
                        QE_INVALIDARG_ERROR(0);
                    }
                    int op = (direction_ing?dvn_op:svn_op);
                    GenDb::Op::type comparator;
                    std::string val = direction_ing?
                        (boost::get<std::string>(dvn)):(boost::get<std::string>(svn));
                    if (op == PREFIX) {
                        if (val == "") {
                            continue;
                        }
                        val += "%";
                        comparator = GenDb::Op::LIKE;
                    } else {
                        comparator = GenDb::Op::EQ;
                    }
                    GenDb::WhereIndexInfo where_info = boost::make_tuple(columnN,
                        comparator, val);
                    server_session_query->where_vec.push_back(where_info);
                }
            }
        }
#endif
#ifndef USE_SESSION
        if (!isSession && vr_match)
        {
            DbQueryUnit *db_query = new DbQueryUnit(this, m_query);
            db_query->row_key_suffix.push_back((uint8_t)direction_ing);
            db_query->cfname = g_viz_constants.FLOW_TABLE_VROUTER;
            db_query->cr.start_.push_back(vr);
            if (vr_op == EQUAL) {
                db_query->cr.finish_.push_back(vr);
            } else if (vr_op == PREFIX) {
                db_query->cr.finish_.push_back(vr2);
            } else {
                QE_LOG(INFO, "Internal Error:" << __func__ << "Unknown vr_op: " << vr_op);
                db_query->cr.finish_.push_back(vr);
            }

            QE_TRACE(DEBUG, "row_key_suffix for vrouter:" << direction_ing);
            QE_TRACE(DEBUG, "vrouter cr.start len:" << 
                    db_query->cr.start_.size() << 
                    " string:" << "TBD");
        }

        // now create flow related db queries
        if (!isSession && svn_match)
        {
            DbQueryUnit *db_query = new DbQueryUnit(this, m_query);
            db_query->row_key_suffix.push_back((uint8_t)direction_ing);
            db_query->cfname = g_viz_constants.FLOW_TABLE_SVN_SIP;
            db_query->cr.start_.push_back(svn);
            if (svn_op == EQUAL) {
                db_query->cr.finish_.push_back(svn);
            } else if (svn_op == PREFIX) {
                db_query->cr.finish_.push_back(svn2);
            } else {
                QE_LOG(INFO, "Internal Error:" << __func__ << "Unknown svn_op: " << svn_op);
                db_query->cr.finish_.push_back(svn2);
            }

            if (sip_match)
            {
                db_query->cr.start_.push_back(sip);
                if (sip_op == IN_RANGE) {
                    db_query->cr.finish_.push_back(sip2);
                } else {
                    db_query->cr.finish_.push_back(sip);
                }
            }  else {
                boost::system::error_code ec;
                db_query->cr.finish_.push_back(IpAddress::from_string(
                    "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", ec));
            }

            QE_TRACE(DEBUG, "row_key_suffix for svn/sip:" << direction_ing);
            QE_TRACE(DEBUG, "svn/sip cr.start len:" << 
                    db_query->cr.start_.size() << 
                    " string:" << "TBD");
        }

        if (!isSession && dvn_match)
        {
            DbQueryUnit *db_query = new DbQueryUnit(this, m_query);
            db_query->row_key_suffix.push_back((uint8_t)direction_ing);
            db_query->cfname = g_viz_constants.FLOW_TABLE_DVN_DIP;
            db_query->cr.start_.push_back(dvn);
            if (dvn_op == EQUAL)
                db_query->cr.finish_.push_back(dvn); // only equal op
            else
                db_query->cr.finish_.push_back(dvn2); // only equal op

            if (dip_match)
            {
                db_query->cr.start_.push_back(dip);
                if (dip_op == IN_RANGE) {
                    db_query->cr.finish_.push_back(dip2);
                } else {
                    db_query->cr.finish_.push_back(dip);
                }
            }  else {
                boost::system::error_code ec;
                db_query->cr.finish_.push_back(IpAddress::from_string(
                    "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", ec));
            }

            QE_TRACE(DEBUG, "row_key_suffix for dvn/dip:" << direction_ing);
            QE_TRACE(DEBUG, "dvn/dip cr.start len:" << 
                    db_query->cr.start_.size() << 
                    " string:" << "TBD");
        }

        if (!isSession && proto_match)
        {
            if (sport_match)
            {
                DbQueryUnit *db_query = new DbQueryUnit(this, m_query);
                db_query->row_key_suffix.push_back((uint8_t)direction_ing);
                db_query->cfname = g_viz_constants.FLOW_TABLE_PROT_SP;

                if (sport_op == EQUAL) {
                    db_query->cr.start_.push_back(proto);
                    db_query->cr.start_.push_back(sport);
                    db_query->cr.finish_ = db_query->cr.start_; // only equal op
                } else {
                    db_query->cr.start_.push_back(proto);
                    db_query->cr.start_.push_back(sport);
                    db_query->cr.finish_.push_back(proto);
                    db_query->cr.finish_.push_back(sport2);
                }

                QE_TRACE(DEBUG, 
                        "row_key_suffix for proto/sp:" << direction_ing);
                QE_TRACE(DEBUG, "proto/sp cr.start len:" << 
                    db_query->cr.start_.size() << 
                    " string:" << "TBD");
            }

            if (dport_match)
            {
                DbQueryUnit *db_query = new DbQueryUnit(this, m_query);
                db_query->row_key_suffix.push_back((uint8_t)direction_ing);
                db_query->cfname = g_viz_constants.FLOW_TABLE_PROT_DP;
                if (dport_op == EQUAL) {
                    db_query->cr.start_.push_back(proto);
                    db_query->cr.start_.push_back(dport);
                    db_query->cr.finish_ = db_query->cr.start_; // only equal op
                } else {
                    db_query->cr.start_.push_back(proto);
                    db_query->cr.start_.push_back(dport);
                    db_query->cr.finish_.push_back(proto);
                    db_query->cr.finish_.push_back(dport2);
                }

                QE_TRACE(DEBUG, 
                        "row_key_suffix for proto/sp:" << direction_ing);
                QE_TRACE(DEBUG, "proto/sp cr.start len:" << 
                    db_query->cr.start_.size() << 
                    " string:" << "TBD");
            }

            if (!(dport_match) && !(sport_match))
            {
                // no port specified, just query for protocol
                DbQueryUnit *db_query = new DbQueryUnit(this, m_query);
                db_query->row_key_suffix.push_back((uint8_t)direction_ing);
                db_query->cfname = g_viz_constants.FLOW_TABLE_PROT_DP;
                db_query->cr.start_.push_back(proto);
                // only equal op
                if (proto_op == EQUAL) {
                    db_query->cr.finish_.push_back(proto);
                    db_query->cr.finish_.push_back((uint16_t)0xffff);
                } else {
                    db_query->cr.finish_.push_back(proto2);
                    db_query->cr.finish_.push_back((uint16_t)0xffff);
                }

                QE_TRACE(DEBUG, 
                        "row_key_suffix for proto:" << direction_ing);
                QE_TRACE(DEBUG, "proto/sp cr.start len:" << 
                    db_query->cr.start_.size() << 
                    " string:" << "TBD");

            }
        }
#endif
    }
}

// For UT
WhereQuery::WhereQuery(QueryUnit *mq): QueryUnit(mq, mq){
}

void WhereQuery::subquery_processed(QueryUnit *subquery) {
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    {
        tbb::mutex::scoped_lock lock(vector_push_mutex_);
        int sub_query_id = ((DbQueryUnit *)subquery)->sub_query_id;
        if (((DbQueryUnit *)subquery)->cfname == g_viz_constants.OBJECT_TABLE) {
           inp.insert(inp.begin(), sub_queries[sub_query_id]->query_result.get());
        } else {
           inp.push_back((sub_queries[sub_query_id]->query_result.get()));
        }
        if (subquery->query_status == QUERY_FAILURE) {
            QE_QUERY_FETCH_ERROR();
        }
        if (sub_queries.size() != inp.size()) {
            return;
        }
    }

    // Handle if any of the sub query has failed.
    if (m_query->qperf_.error) {
        m_query->qperf_.chunk_where_time =
        static_cast<uint32_t>((UTCTimestampUsec() - m_query->where_start_)
        /1000);
        where_query_cb_(m_query->handle_, m_query->qperf_, where_result_);
        return;
    }
    if (m_query->is_message_table_query()
        || m_query->is_object_table_query(m_query->table())
#ifdef USE_SESSION
        || m_query->is_flow_query(m_query->table())
#endif
        ) {
        SetOperationUnit::op_or(((AnalyticsQuery *)(this->main_query))->query_id,
            *where_result_, inp);
    } else {
        SetOperationUnit::op_and(((AnalyticsQuery *)(this->main_query))->query_id,
            *where_result_, inp);
    }
    m_query->query_status = query_status;

    QE_TRACE(DEBUG, "Set ops returns # of rows:" << where_result_->size());

#ifndef USE_SESSION
    if (m_query->table() == g_viz_constants.FLOW_TABLE) {
        // weed out duplicates
        QE_TRACE(DEBUG,
            "Weeding out duplicates for the Flow Records Table query");
        std::vector<query_result_unit_t> uniqued_result;
        std::map<boost::uuids::uuid, int> uuid_list;
        // reverse iterate to make sure the latest entries are there
        for (int i = (int)(where_result_->size() -1); i>=0; i--) {
            boost::uuids::uuid u; flow_stats stats;
            where_result_->at(i).get_uuid_stats(u, stats);
            std::map<boost::uuids::uuid, int>::iterator it;
            it = uuid_list.find(u);
            if (it == uuid_list.end()) {
                uuid_list.insert(std::pair<boost::uuids::uuid, int>(u, 0));
                // this is first instance of the UUID, hence insert in the
                // results table
                uniqued_result.push_back(where_result_->at(i));
            }
        }
        *where_result_ = uniqued_result;
    }
#endif
    // Have the result ready and processing is done
    QE_TRACE(DEBUG, "WHERE processing done row #s:" <<
         where_result_->size());
    QE_TRACE_NOQID(DEBUG, " Finished where processing for QID " << m_query->query_id
        << " chunk:" << m_query->parallel_batch_num);
    status_details = 0;
    parent_query->subquery_processed(this);
    m_query->status_details = status_details;
    m_query->qperf_.chunk_where_time =
        static_cast<uint32_t>((UTCTimestampUsec() - m_query->where_start_)
        /1000);
    where_query_cb_(m_query->handle_, m_query->qperf_, where_result_);
}

query_status_t WhereQuery::process_query()
{
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;

    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return QUERY_FAILURE;
    }

    QE_TRACE(DEBUG, "WhereQuery" );

    QE_TRACE(DEBUG, "Starting processing of " << sub_queries.size() <<
            " subqueries");

    if (m_query->table() == g_viz_constants.OBJECT_VALUE_TABLE) {
        status_details = 0;
        parent_query->subquery_processed(this);
        return QUERY_SUCCESS;
    }
    unsigned int v_size = sub_queries.size();
    // invoke processing of all the sub queries
    // TBD: Handle ASYNC processing
    for (unsigned int i = 0; i < v_size; i++)
    {
        query_status = sub_queries[i]->process_query();
        if (query_status == QUERY_FAILURE) {
            return query_status;
        }
    }
    return query_status;
}

// We need to cover 2 cases here in MessageTablev2
// (a) --object-type is specified without any --object-id
// (b) --object-type and --object-id are specified

// (a) ObjectTypeValue fields are stored in following format
//  T2:ObjectType:ObjectId
//  We need to query for T2:ObjectType*
// (b) We have 6 columns to save OBJECTID.
// Any OBJECTID could be in any of the 6 columns.
// For OBJECTID query, we need to check each of the 6 columns.
// Since its an OR operation, we need to create 6 queries, one
// for each column.
// Combining (a) & (b) we end up creating 6 queries 1 for each
// ObjectTypeValue[1..6] column.
void WhereQuery::handle_object_type_value(
                                    AnalyticsQuery *m_query,
                                    DbQueryUnit *db_query,
                                    bool object_id_specified)
{
    if (m_query->is_object_table_query(m_query->table())) {
        QE_TRACE(DEBUG, "object-type-value handling");
        std::string column1 = MsgTableQueryColumnToColumn(
                                        g_viz_constants.OBJECT_TYPE_NAME1);
        if (object_id_specified == false) {
            // create db_query entry for OBJECT_TYPE_NAME1
            // as done for OBJECTID case above.
            // rest falls in place as with --object-id case.
            match_op op = PREFIX;
            std::string value_prefix = m_query->table();
            value_prefix.append(":");
            std::string col_name = g_viz_constants.OBJECT_TYPE_NAME1;
            populate_where_vec(db_query, col_name, op,
                               value_prefix, std::string(""));
        }

        // regular --object-id processing from here
        int index = 0;
        BOOST_FOREACH(GenDb::WhereIndexInfo &where_info, db_query->where_vec) {
            if (column1 == where_info.get<0>()) {
                break;
            }
            index++;
        }

        // OBJECT_TYPE_NAME1 is already done above
        for (int i = 2;
             i <= g_viz_constants.MSG_TABLE_MAX_OBJECTS_PER_MSG;
             i++) {
            DbQueryUnit *msg_table_db_query2 = new DbQueryUnit(this, main_query);
            msg_table_db_query2->cfname = g_viz_constants.COLLECTOR_GLOBAL_TABLE;
            msg_table_db_query2->t_only_row = true;
            msg_table_db_query2->t_only_col = true;
            msg_table_db_query2->where_vec = db_query->where_vec;

            GenDb::WhereIndexInfo *where_info2 = &msg_table_db_query2->where_vec[index];
            std::string col_name = g_viz_constants.OBJECT_TYPE_NAME_PFX;
            col_name.append(integerToString(i));

            std::string columnN = MsgTableQueryColumnToColumn(col_name);
            where_info2->get<0>() = columnN;
        }
    }
}

void WhereQuery::populate_where_vec(DbQueryUnit *db_query,
                                    const std::string query_col,
                                    match_op op,
                                    const std::string value_prefix,
                                    const std::string value)
{
    std::string value2 = value_prefix;
    value2.append(value);
    if (op == PREFIX) {
        value2.append("%");
    }
    if (value2 == "%") {
        return;
    }
    std::string columnN = MsgTableQueryColumnToColumn(query_col);
    GenDb::WhereIndexInfo where_info =
            boost::make_tuple(columnN, get_gendb_op_from_op(op), value2);
    db_query->where_vec.push_back(where_info);
}
