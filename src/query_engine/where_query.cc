/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cstdlib>
#include <limits> 
#include "rapidjson/document.h"
#include "query.h"
#include "json_parse.h"
#include "stats_query.h"

static std::string ToString(const rapidjson::Value& value_value) {
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

static GenDb::DbDataValue ToDbDataValue(const rapidjson::Value& val) {
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

bool
WhereQuery::StatTermParse(QueryUnit *main_query, const rapidjson::Value& where_term,
        std::string& pname, match_op& pop, GenDb::DbDataValue& pval, GenDb::DbDataValue& pval2,
        std::string& sname, match_op& sop, GenDb::DbDataValue& sval, GenDb::DbDataValue& sval2) {

    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    QE_ASSERT(m_query->is_stat_table_query());

    std::string srvalstr, srval2str;

    const rapidjson::Value& name_value = 
	where_term[WHERE_MATCH_NAME];
    if (!name_value.IsString()) return false;
    pname = name_value.GetString();
   
    const rapidjson::Value& prval = where_term[WHERE_MATCH_VALUE];
    if (!((prval.IsString() || prval.IsNumber()))) return false;
    const rapidjson::Value& prval2 = where_term[WHERE_MATCH_VALUE2];

    // For dynamic stat tables, convert types as per query json
    pval = ToDbDataValue(prval);
    pval2 = ToDbDataValue(prval2);
    
    const rapidjson::Value& op_value =
        where_term[WHERE_MATCH_OP];
    if (!op_value.IsNumber()) return false;
    pop = (match_op)op_value.GetInt();

    const rapidjson::Value& suffix =
        where_term[WHERE_MATCH_SUFFIX];
  
     
    QE_TRACE(DEBUG, "StatTable Where Term Prefix " << pname << " val " << ToString(prval) 
            << " val2 " << ToString(prval2) << " op " << pop);

    sname = std::string(); 
    sop = (match_op)0;
    if (suffix.IsObject()) {
        // For prefix-suffix where terms, prefix operator MUST be "EQUAL"
        if (pop != EQUAL) return false;

        // For prefix-suffix where terms, prefix value2 MUST be Null
        if (!prval2.IsNull()) return false;

        const rapidjson::Value& svalue_value =
            suffix[WHERE_MATCH_VALUE];
        if (!((svalue_value.IsString() || svalue_value.IsNumber()))) return false;
        srvalstr = ToString(svalue_value);
        // For dynamic stat tables, convert types as per query json
        sval = ToDbDataValue(svalue_value);

        const rapidjson::Value& svalue2_value =
            suffix[WHERE_MATCH_VALUE2];
        srval2str = ToString(svalue2_value);
        // For dynamic stat tables, convert types as per query json
        sval2 = ToDbDataValue(svalue2_value);

        const rapidjson::Value& sop_value =
            suffix[WHERE_MATCH_OP];
        if (!sop_value.IsNumber()) return false;
        sop = (match_op)sop_value.GetInt();

        const rapidjson::Value& sname_value =
            suffix[WHERE_MATCH_NAME];
        if (!sname_value.IsString()) return false;
        sname = sname_value.GetString();

    }

    QE_TRACE(DEBUG, "StatTable Where Term Suffix" << sname << " val " << srvalstr
            << " val2 " << srval2str << " op " << sop);
     
    if (m_query->stats().is_stat_table_static()) {
        // For static tables, check that prefix is valid and convert types as per schema
        StatsQuery::column_t cdesc = m_query->stats().get_column_desc(pname);

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
                StatsQuery::column_t cdesc2 = m_query->stats().get_column_desc(sname);
                
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

                StatsQuery::column_t cdesc2 = m_query->stats().get_column_desc(sname);
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

bool WhereQuery::StatTermProcess(const rapidjson::Value& where_term,
        SetOperationUnit * and_node, QueryUnit *main_query) {

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

WhereQuery::WhereQuery(const std::string& where_json_string, int direction,
        QueryUnit *main_query): 
    QueryUnit(main_query, main_query), direction_ing(direction),
    json_string_(where_json_string) {

    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;

    if (where_json_string == std::string(""))
    {
        DbQueryUnit *db_query = new DbQueryUnit(this, main_query);

        //TBD not sure if this will work for Message table or Object Log
        if (m_query->table() == g_viz_constants.COLLECTOR_GLOBAL_TABLE) {
            db_query->cfname = g_viz_constants.MESSAGE_TABLE_TIMESTAMP;
            db_query->t_only_col = true;
            db_query->t_only_row = true;
        } else if 
        ((m_query->table() == g_viz_constants.FLOW_TABLE)
        || (m_query->table() == g_viz_constants.FLOW_SERIES_TABLE)) {

            db_query->row_key_suffix.push_back((uint8_t)direction_ing);
            db_query->cfname = g_viz_constants.FLOW_TABLE_PROT_SP;

            // starting value for protocol/port field
            db_query->cr.start_.push_back((uint8_t)0);

            // ending value for protocol/port field;
            db_query->cr.finish_.push_back((uint8_t)0xff);
            db_query->cr.finish_.push_back((uint16_t)0xffff);

        } else if (m_query->is_object_table_query()) {
            // These values will encompass all possible ascii strings in their range
            GenDb::DbDataValue value = "\x1b", value2 = "\x7f";

            db_query->cfname = g_viz_constants.OBJECT_TABLE;
            db_query->row_key_suffix.push_back(m_query->table());

            // Added object id to column
            db_query->cr.start_.push_back(value);
            db_query->cr.finish_.push_back(value2);

            QE_TRACE(DEBUG, "where * for object table" << m_query->table());
        }

        // This is "where *" query, no need to do JSON parsing
        return;
    }

    // Do JSON parsing
    // highest level set operation node;
    SetOperationUnit *or_node= new SetOperationUnit(this, main_query);
    rapidjson::Document d;
    std::string json_string = "{ \"where\" : " + 
        where_json_string + " }";

    QE_TRACE(DEBUG, "where query:" << json_string);
    d.Parse<0>(const_cast<char *>(json_string.c_str()));
    const rapidjson::Value& json_or_list = d["where"]; 
    QE_PARSE_ERROR(json_or_list.IsArray());

    QE_TRACE(DEBUG, "number of OR terms in where :" << json_or_list.Size());

    for (rapidjson::SizeType i = 0; i < json_or_list.Size(); i++) 
    {
        SetOperationUnit *and_node= 
            new SetOperationUnit(or_node, main_query);
        and_node->set_operation = SetOperationUnit::INTERSECTION_OP;
        and_node->is_leaf_node = true;

        QE_PARSE_ERROR(json_or_list[i].IsArray());
        QE_INVALIDARG_ERROR(json_or_list[i].Size() != 0);

        const rapidjson::Value& json_or_node = json_or_list[i];

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

        for (rapidjson::SizeType j = 0; j < json_or_node.Size(); j++)
        {
            const rapidjson::Value& name_value = 
                json_or_node[j][WHERE_MATCH_NAME];
            const rapidjson::Value&  value_value = 
                json_or_node[j][WHERE_MATCH_VALUE];
            const rapidjson::Value& op_value = 
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
                const rapidjson::Value&  value_value2 = 
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

            bool isStat = m_query->is_stat_table_query();
            if ((name == g_viz_constants.SOURCE) && (!isStat))
            {
                DbQueryUnit *db_query = new DbQueryUnit(and_node, main_query);

                db_query->cfname = g_viz_constants.MESSAGE_TABLE_SOURCE;
                db_query->t_only_col = true;

                // only EQUAL op supported currently 
                QE_INVALIDARG_ERROR(op == EQUAL);

                // string encoding
                db_query->row_key_suffix.push_back(value);

                QE_TRACE(DEBUG, "where match term for source " << value);
            }

            if ((name == g_viz_constants.KEYWORD) && (!isStat))
            {
                DbQueryUnit *db_query = new DbQueryUnit(and_node, main_query);

                db_query->cfname = g_viz_constants.MESSAGE_TABLE_KEYWORD;
                db_query->t_only_col = true;

                // only EQUAL op supported currently
                QE_INVALIDARG_ERROR(op == EQUAL);

                // string encoding
                db_query->row_key_suffix.push_back(value);

                QE_TRACE(DEBUG, "where match term for source " << value);
            }

            if ((name == g_viz_constants.MODULE) && (!isStat))
            {
                DbQueryUnit *db_query = new DbQueryUnit(and_node, main_query);
                db_query->cfname = g_viz_constants.MESSAGE_TABLE_MODULE_ID;
                db_query->t_only_col = true;

                // only EQUAL op supported currently 
                QE_INVALIDARG_ERROR(op == EQUAL);

                // string encoding
                db_query->row_key_suffix.push_back(value);

                // dont filter query engine logs if the query is about query
                // engine
                if (value == m_query->sandesh_moduleid)
                    m_query->filter_qe_logs = false;

                QE_TRACE(DEBUG, "where match term for module " << value);
            }
 
            if ((name == g_viz_constants.MESSAGE_TYPE) && (!isStat))
            {
                DbQueryUnit *db_query = new DbQueryUnit(and_node, main_query);
                db_query->cfname = 
                    g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE;
                db_query->t_only_col = true;

                // only EQUAL op supported currently 
                QE_INVALIDARG_ERROR(op == EQUAL);

                // string encoding
                db_query->row_key_suffix.push_back(value);

                QE_TRACE(DEBUG, "where match term for msg-type " << value);
            }
  
            if ((name == g_viz_constants.CATEGORY) && (!isStat))
            {
                DbQueryUnit *db_query = new DbQueryUnit(and_node, main_query);
                db_query->cfname = 
                    g_viz_constants.MESSAGE_TABLE_CATEGORY;
                db_query->t_only_col = true;

                // only EQUAL op supported currently 
                QE_INVALIDARG_ERROR(op == EQUAL);

                // string encoding
                db_query->row_key_suffix.push_back(value);

                QE_TRACE(DEBUG, "where match term for msg-type " << value);
            }

            if (name == OBJECTID)
            {
                DbQueryUnit *db_query = new DbQueryUnit(and_node, main_query);
                GenDb::DbDataValue value2 = value;

                db_query->cfname = g_viz_constants.OBJECT_TABLE;
                db_query->row_key_suffix.push_back(m_query->table());

                // only EQUAL or PREFIX op supported currently 
                QE_INVALIDARG_ERROR((op == EQUAL) || (op == PREFIX));
                if (op == PREFIX)
                {
                    value2 = value + "\x7f";
                }

                // Added object id to column
                db_query->cr.start_.push_back(value);
                db_query->cr.finish_.push_back(value2);

                QE_TRACE(DEBUG, "where match term for objectid " << value);
                object_id_specified = true;
            }

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

                struct in_addr addr; 
                QE_INVALIDARG_ERROR(inet_aton(value.c_str(), &addr) != -1);

                sip = (uint32_t)htonl(addr.s_addr);

                if (sip_op == IN_RANGE)
                {
                    struct in_addr addr; 
                    QE_INVALIDARG_ERROR(
                            inet_aton(value2.c_str(), &addr) != -1);
                    sip2 = htonl(addr.s_addr);
                } else {
                    QE_INVALIDARG_ERROR(sip_op == EQUAL);
                }

                QE_TRACE(DEBUG, 
        "where match term for sourceip " << value << ":" << addr.s_addr);
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

                struct in_addr addr; 
                QE_INVALIDARG_ERROR(inet_aton(value.c_str(), &addr) != -1);

                dip = (uint32_t)htonl(addr.s_addr);
                if (dip_op == IN_RANGE)
                {
                    struct in_addr addr; 
                    QE_INVALIDARG_ERROR(
                            inet_aton(value2.c_str(), &addr) != -1);
                    dip2 = htonl(addr.s_addr);
                } else {
                    QE_INVALIDARG_ERROR(dip_op == EQUAL);
                }

                QE_TRACE(DEBUG, 
        "where match term for destip " << value << ":" << addr.s_addr);
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

            if (isStat)
            {
                StatTermProcess(json_or_node[j], and_node, main_query);
                object_id_specified = true;
            }
        }

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

        if (vr_match) 
        {
            DbQueryUnit *db_query = new DbQueryUnit(and_node, m_query);
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
        if (svn_match) 
        {
            DbQueryUnit *db_query = new DbQueryUnit(and_node, m_query);
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
                db_query->cr.finish_.push_back((uint32_t)0xffffffff);
            }

            QE_TRACE(DEBUG, "row_key_suffix for svn/sip:" << direction_ing);
            QE_TRACE(DEBUG, "svn/sip cr.start len:" << 
                    db_query->cr.start_.size() << 
                    " string:" << "TBD");
        }

        if (dvn_match) 
        {
            DbQueryUnit *db_query = new DbQueryUnit(and_node, m_query);
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
                db_query->cr.finish_.push_back((uint32_t)0xffffffff);
            }

            QE_TRACE(DEBUG, "row_key_suffix for dvn/dip:" << direction_ing);
            QE_TRACE(DEBUG, "dvn/dip cr.start len:" << 
                    db_query->cr.start_.size() << 
                    " string:" << "TBD");
        }

        if (proto_match)
        {
            if (sport_match)
            {
                DbQueryUnit *db_query = new DbQueryUnit(and_node, m_query);
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
                DbQueryUnit *db_query = new DbQueryUnit(and_node, m_query);
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
                DbQueryUnit *db_query = new DbQueryUnit(and_node, m_query);
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

        if (m_query->is_object_table_query())
        {
            // object id table query
            if (!object_id_specified)
            {
                QE_LOG(DEBUG, "Object id needs to be specified");
                QE_INVALIDARG_ERROR(0);
            }
        }
        
    }
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

    // invoke processing of all the sub queries
    // TBD: Handle ASYNC processing
    for (unsigned int i = 0; i < sub_queries.size(); i++)
    {
        query_status_t query_status = sub_queries[i]->process_query();

        if (query_status == QUERY_FAILURE)
        {
            status_details = sub_queries[i]->status_details;
            parent_query->subquery_processed(this);
            return QUERY_FAILURE;
        }
    }

    // TBD make this generic 
    if (sub_queries.size() > 0)
        query_result = sub_queries[0]->query_result;

    QE_TRACE(DEBUG, "Set ops returns # of rows:" << query_result.size());

    if (m_query->table() == g_viz_constants.FLOW_TABLE)
    {
        // weed out duplicates
        QE_TRACE(DEBUG, 
                "Weeding out duplicates for the Flow Records Table query");
        std::vector<query_result_unit_t> uniqued_result;
        std::map<boost::uuids::uuid, int> uuid_list;

        // reverse iterate to make sure the latest entries are there
        for (int i = (int)(query_result.size() -1); i>=0; i--)
        {
            boost::uuids::uuid u; flow_stats stats;
            query_result[i].get_uuid_stats(u, stats);
            std::map<boost::uuids::uuid, int>::iterator it;
            it = uuid_list.find(u);
            if (it == uuid_list.end())
            {
                uuid_list.insert(std::pair<boost::uuids::uuid, int>(u, 0));
                // this is first instance of the UUID, hence insert in the 
                // results table
                uniqued_result.push_back(query_result[i]);
            }
        }

        query_result = uniqued_result;  // only unique values of UUID return
    }

    // Have the result ready and processing is done
    QE_TRACE(DEBUG, "WHERE processing done row #s:" << query_result.size());
    status_details = 0;
    parent_query->subquery_processed(this);
    return QUERY_SUCCESS;
}
