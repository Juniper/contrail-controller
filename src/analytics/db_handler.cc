/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "db_handler.h"
#include <exception>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include "io/event_manager.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_session.h"
#include "viz_constants.h"
#include "vizd_table_desc.h"

DbHandler::DbHandler(EventManager *evm,
        GenDb::GenDbIf::DbErrorHandler err_handler,
        std::string cassandra_ip, unsigned short cassandra_port, int analytics_ttl) :
    dbif_(GenDb::GenDbIf::GenDbIfImpl(evm->io_service(), err_handler,
                cassandra_ip, cassandra_port, true, analytics_ttl*24*3600)) {
}

DbHandler::DbHandler(GenDb::GenDbIf *dbif) :
    dbif_(dbif) {
}

DbHandler::~DbHandler() {
}

bool DbHandler::CreateTables() {
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_tables.begin();
            it != vizd_tables.end(); it++) {
        if (!dbif_->NewDb_AddColumnfamily(*it)) {
            VIZD_ASSERT(0);
        }
    }

    /* create ObjectTables */
    for (std::map<std::string, objtable_info>::const_iterator it = g_viz_constants._OBJECT_TABLES.begin(); it != g_viz_constants._OBJECT_TABLES.end(); it++) {
        if (!dbif_->NewDb_AddColumnfamily(
                    (GenDb::NewCf(it->first,
                                  boost::assign::list_of
                                  (GenDb::DbDataType::Unsigned32Type)
                                  (GenDb::DbDataType::AsciiType),
                                  boost::assign::list_of
                                  (GenDb::DbDataType::Unsigned32Type),
                                  boost::assign::list_of
                                  (GenDb::DbDataType::LexicalUUIDType))))) {
            VIZD_ASSERT(0);
        }
    }

    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_flow_tables.begin();
            it != vizd_flow_tables.end(); it++) {
        if (!dbif_->NewDb_AddColumnfamily(*it)) {
            VIZD_ASSERT(0);
        }
    }

    GenDb::ColList col_list;
    std::string cfname = g_viz_constants.SYSTEM_OBJECT_TABLE;
    GenDb::DbDataValueVec key;
    key.push_back(g_viz_constants.SYSTEM_OBJECT_ANALYTICS);

    bool init_done = false;
    if (dbif_->Db_GetRow(col_list, cfname, key)) {
        for (std::vector<GenDb::NewCol>::iterator it = col_list.columns_.begin();
                it != col_list.columns_.end(); it++) {
            std::string col_name;
            try {
                col_name = boost::get<std::string>(it->name[0]);
            } catch (boost::bad_get& ex) {
                LOG(ERROR, __func__ << ": Exception on col_name get");
            }

            if (col_name == g_viz_constants.SYSTEM_OBJECT_START_TIME) {
                init_done = true;
            }
        }
    }

    if (!init_done) {
        GenDb::ColList *col_list(new GenDb::ColList);

        col_list->cfname_ = g_viz_constants.SYSTEM_OBJECT_TABLE;

        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
        rowkey.push_back(g_viz_constants.SYSTEM_OBJECT_ANALYTICS);

        std::vector<GenDb::NewCol>& columns = col_list->columns_;

        columns.push_back(GenDb::NewCol(g_viz_constants.SYSTEM_OBJECT_START_TIME, UTCTimestampUsec()));

        std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);
        if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
            VIZD_ASSERT(0);
        }
    }

    return true;
}

void DbHandler::UnInit() {
    dbif_->Db_Uninit();
}

bool DbHandler::Init() {
    /* init of vizd table structures */
    init_vizd_tables();

    LOG(DEBUG, "DbHandler::" << __func__ << " Begin");
    if (!dbif_->Db_Init()) {
        LOG(ERROR, __func__ << ": Connection to DB FAILED");
        return false;
    }

    if (!dbif_->Db_AddSetTablespace(g_viz_constants.COLLECTOR_KEYSPACE)) {
        LOG(ERROR, __func__ << ": Create/Set KEYSPACE: " <<
            g_viz_constants.COLLECTOR_KEYSPACE << " FAILED");
        return false;
    }

    if (!CreateTables()) {
        LOG(ERROR, __func__ << ": CreateTables failed");
        return false;
    }

    LOG(DEBUG, "DbHandler::" << __func__ << " Done");

    return true;
}

inline bool DbHandler::AllowMessageTableInsert(std::string& message_type) {
    return message_type != "FlowDataIpv4Object";
}

inline bool DbHandler::MessageIndexTableInsert(const std::string& cfname,
        const SandeshHeader& header,
        const std::string& message_type,
        const boost::uuids::uuid& unm) {
    GenDb::ColList *col_list(new GenDb::ColList);

    col_list->cfname_ = cfname;
    GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

    rowkey.push_back((uint32_t)(header.get_Timestamp() >> g_viz_constants.RowTimeInBits));
    if (cfname == g_viz_constants.MESSAGE_TABLE_SOURCE) {
            rowkey.push_back(header.get_Source());
    } else if (cfname == g_viz_constants.MESSAGE_TABLE_MODULE_ID) {
            rowkey.push_back(header.get_Module());
    } else if (cfname == g_viz_constants.MESSAGE_TABLE_CATEGORY) {
            rowkey.push_back(header.get_Category());
    } else if (cfname == g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE) {
            rowkey.push_back(message_type);
    } else if (cfname == g_viz_constants.MESSAGE_TABLE_TIMESTAMP) {
    } else {
        LOG(ERROR, __func__ << ": Addition of message: " << message_type <<
                " to table:" << cfname << " FAILED");
        return false;
    }

    std::vector<GenDb::NewCol>& columns = col_list->columns_;

    GenDb::DbDataValueVec col_name;
    col_name.push_back((uint32_t)(header.get_Timestamp() & g_viz_constants.RowTimeInMask));

    GenDb::DbDataValueVec col_value;
    col_value.push_back(unm);

    columns.push_back(GenDb::NewCol(col_name, col_value));

    std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);
    if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
        VIZD_ASSERT(0);
    }
    return true;
}

void DbHandler::MessageTableInsert(boost::shared_ptr<VizMsg> vmsgp) {
    SandeshHeader header(vmsgp->hdr);
    std::string message_type(vmsgp->messagetype);

    if (!AllowMessageTableInsert(message_type))
        return;

    uint64_t temp_u64;
    uint32_t temp_u32;
    std::string temp_str;

    GenDb::ColList *col_list(new GenDb::ColList);
    col_list->cfname_ = g_viz_constants.COLLECTOR_GLOBAL_TABLE;

    GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
    rowkey.push_back(vmsgp->unm);

    std::vector<GenDb::NewCol>& columns = col_list->columns_;
    columns.push_back(GenDb::NewCol(g_viz_constants.SOURCE, header.get_Source()));
    columns.push_back(GenDb::NewCol(g_viz_constants.NAMESPACE, header.get_Namespace()));
    columns.push_back(GenDb::NewCol(g_viz_constants.MODULE, header.get_Module()));
    if (!header.get_Context().empty()) {
        columns.push_back(GenDb::NewCol(g_viz_constants.CONTEXT, header.get_Context()));
    }
    // Convert to network byte order
    temp_u64 = header.get_Timestamp();
    columns.push_back(GenDb::NewCol(g_viz_constants.TIMESTAMP, temp_u64));

    columns.push_back(GenDb::NewCol(g_viz_constants.CATEGORY, header.get_Category()));

    temp_u32 = header.get_Level();
    columns.push_back(GenDb::NewCol(g_viz_constants.LEVEL, temp_u32));

    columns.push_back(GenDb::NewCol(g_viz_constants.MESSAGE_TYPE, message_type));

    temp_u32 = header.get_SequenceNum();
    columns.push_back(GenDb::NewCol(g_viz_constants.SEQUENCE_NUM, temp_u32));

    temp_u32 = header.get_VersionSig();
    columns.push_back(GenDb::NewCol(g_viz_constants.VERSION, temp_u32));

    uint8_t temp_u8 = header.get_Type();
    columns.push_back(GenDb::NewCol(g_viz_constants.SANDESH_TYPE, temp_u8));

    columns.push_back(GenDb::NewCol(g_viz_constants.DATA, vmsgp->xmlmessage));

    std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);
    if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
        LOG(ERROR, __func__ << ": Addition of message: " << message_type <<
                " COLUMN FAILED");
    }

    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_SOURCE, header, message_type, vmsgp->unm);
    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_MODULE_ID, header, message_type, vmsgp->unm);
    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_CATEGORY, header, message_type, vmsgp->unm);
    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE, header, message_type, vmsgp->unm);
    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_TIMESTAMP, header, message_type, vmsgp->unm);
}

void DbHandler::GetRuleMap(RuleMap& rulemap) {
}

/*
 * insert an entry into an ObjectTrace table
 * key is T2:<key>
 * column is
 *  name: T1 (value in timestamp)
 *  value: uuid (of the corresponding global message)
 */
void DbHandler::ObjectTableInsert(const std::string table, const std::string rowkey_str,
        const RuleMsg& rmsg, const boost::uuids::uuid& unm) {
    uint64_t temp_u64;
    uint32_t temp_u32;

    temp_u64 = rmsg.hdr.get_Timestamp();
    temp_u32 = temp_u64 >> g_viz_constants.RowTimeInBits;

      {
        GenDb::ColList *col_list(new GenDb::ColList);

        col_list->cfname_ = table;
        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

        rowkey.push_back(temp_u32);
        rowkey.push_back(rowkey_str);

        std::vector<GenDb::NewCol>& columns = col_list->columns_;

        GenDb::DbDataValueVec col_name;
        col_name.push_back((uint32_t)(temp_u64& g_viz_constants.RowTimeInMask));

        GenDb::DbDataValueVec col_value;
        col_value.push_back(unm);

        columns.push_back(GenDb::NewCol(col_name, col_value));

        std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);
        if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
            VIZD_ASSERT(0);
        }
      }

      {
        GenDb::ColList *col_list(new GenDb::ColList);

        col_list->cfname_ = g_viz_constants.OBJECT_VALUE_TABLE;
        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

        rowkey.push_back(temp_u32);
        rowkey.push_back(table);

        std::vector<GenDb::NewCol>& columns = col_list->columns_;

        GenDb::DbDataValueVec col_name;
        col_name.push_back((uint32_t)(temp_u64& g_viz_constants.RowTimeInMask));

        GenDb::DbDataValueVec col_value;
        col_value.push_back(rowkey_str);

        columns.push_back(GenDb::NewCol(col_name, col_value));

        std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);
        if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
            VIZD_ASSERT(0);
        }
      }
}

/* walker to go through all nodes and insert the node that matches listed fields
 * in name_to_type, into the db
 */
bool FlowDataIpv4ObjectWalker::for_each(pugi::xml_node& node) {
    std::vector<GenDb::NewCf>::const_iterator fit;
    for (fit = vizd_flow_tables.begin(); fit != vizd_flow_tables.end(); fit++) {
        if (fit->cfname_ == g_viz_constants.FLOW_TABLE)
            break;
    }
    if (fit == vizd_flow_tables.end())
        VIZD_ASSERT(0);

    const GenDb::NewCf::SqlColumnMap& sql_cols = fit->cfcolumns_;
    std::vector<GenDb::NewCol>& columns = col_list->columns_;

    GenDb::NewCf::SqlColumnMap::const_iterator it = sql_cols.find(node.name());
    if (it != sql_cols.end()) {
        std::string col_name(node.name());
        GenDb::DbDataValue col_value;
        switch (it->second) {
            case GenDb::DbDataType::Unsigned8Type:
                  {
                    uint8_t val;
                    stringToInteger(node.child_value(), val);
                    col_value = val;
                    break;
                  }
            case GenDb::DbDataType::Unsigned16Type:
                  {
                    int16_t val;
                    stringToInteger(node.child_value(), val);
                    col_value = (uint16_t)val;
                    break;
                  }
            case GenDb::DbDataType::Unsigned32Type:
                  {
                    int32_t val;
                    stringToInteger(node.child_value(), val);
                    col_value = (uint32_t)val;
                    break;
                  }
            case GenDb::DbDataType::Unsigned64Type:
                  {
                    int64_t val;
                    stringToInteger(node.child_value(), val);
                    col_value = (uint64_t)val;
                    break;
                  }
            default:
                std::string val = node.child_value();
                col_value = val;
        }
        columns.push_back(GenDb::NewCol(col_name, col_value));
    }

    return true;
}

/*
 * process the flow message and insert into appropriate tables
 */
bool DbHandler::FlowTableInsert(const RuleMsg& rmsg) {
    pugi::xml_node parent = rmsg.get_doc();

    RuleMsg::RuleMsgPredicate pugi_p(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_FLOWUUID)->second);
    pugi::xml_node flownode = parent.find_node(pugi_p);
    if (!flownode) {
        return false;
    }

    std::string flowu_str = flownode.child_value();
    boost::uuids::uuid flowu = boost::uuids::string_generator()(flowu_str);

    // insert into flow global table
    GenDb::ColList *col_list(new GenDb::ColList);
    FlowDataIpv4ObjectWalker flow_walker(col_list);

    col_list->cfname_ = g_viz_constants.FLOW_TABLE;
    std::vector<GenDb::NewCol>& columns = col_list->columns_;
    columns.push_back(GenDb::NewCol(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_VROUTER)->second,
                rmsg.hdr.get_Source()));

    GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
    rowkey.push_back(flowu);

    if (!parent.traverse(flow_walker)) {
        VIZD_ASSERT(0);
    }

    std::auto_ptr<GenDb::ColList> col_list_ptr(flow_walker.col_list);
    if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
        VIZD_ASSERT(0);
    }

    // insert into vn2vn flow index table
    pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DIFF_BYTES)->second);
    pugi::xml_node bytes_node = parent.find_node(pugi_p);
    pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DIFF_PACKETS)->second);
    pugi::xml_node pkts_node = parent.find_node(pugi_p);
    if ((bytes_node.type() != pugi::node_null) &&
            (pkts_node.type() != pugi::node_null)) {

            uint64_t t;
            uint32_t t2;
            uint32_t t1;
            pugi::xml_node runnode;
            int32_t runint32;
            std::string runstring;

            t = rmsg.hdr.get_Timestamp();
            t2 = t >> g_viz_constants.RowTimeInBits;
            t1 = t & g_viz_constants.RowTimeInMask;

            /* setup the column-value */
            GenDb::DbDataValueVec col_value;

            uint64_t bytes, pkts;
            stringToInteger(bytes_node.child_value(), bytes);
            stringToInteger(pkts_node.child_value(), pkts);
            col_value.push_back(bytes);
            col_value.push_back(pkts);
            // Is this a short flow - both setup_time and teardown_time
            // are present?
            bool short_flow = false;
            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SETUP_TIME)->second);
            runnode = parent.find_node(pugi_p);
            if (runnode.type() != pugi::node_null) {
                pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_TEARDOWN_TIME)->second);
                runnode = parent.find_node(pugi_p);
                if (runnode.type() != pugi::node_null) {
                    short_flow = true;
                }
            }
            runint32 = short_flow ? 1 : 0;
            col_value.push_back((uint8_t)runint32);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_FLOWUUID)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            boost::uuids::uuid uuidval = boost::uuids::string_generator()(runnode.child_value());
            col_value.push_back(uuidval);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DIRECTION_ING)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            uint8_t dir_val = (uint8_t)runint32;
        /* insert into index tables */
          {
            GenDb::ColList *col_list(new GenDb::ColList);

            /* Table */
            col_list->cfname_ = g_viz_constants.FLOW_TABLE_SVN_SIP;

            GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

            int32_t runint32;

            /* setup the rowkey */
            rowkey.push_back(t2);
            rowkey.push_back(dir_val);

            std::vector<GenDb::NewCol>& columns = col_list->columns_;

            GenDb::DbDataValueVec col_name;
            /* setup the column-name */
            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SOURCEVN)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            runstring = runnode.child_value();
            col_name.push_back(runstring);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SOURCEIP)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint32_t)runint32);

            /* T1 */
            col_name.push_back(t1);

            columns.push_back(GenDb::NewCol(col_name, col_value));

            std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);

            if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
                VIZD_ASSERT(0);
            }
          }
          {
            GenDb::ColList *col_list(new GenDb::ColList);

            /* Table */
            col_list->cfname_ = g_viz_constants.FLOW_TABLE_DVN_DIP;

            GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

            pugi::xml_node runnode;
            int32_t runint32;

            /* setup the rowkey */
            rowkey.push_back(t2);
            rowkey.push_back(dir_val);

            std::vector<GenDb::NewCol>& columns = col_list->columns_;

            GenDb::DbDataValueVec col_name;
            /* setup the column-name */
            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DESTVN)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            runstring = runnode.child_value();
            col_name.push_back(runstring);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DESTIP)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint32_t)runint32);

            /* T1 */
            col_name.push_back(t1);

            columns.push_back(GenDb::NewCol(col_name, col_value));

            std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);

            if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
                VIZD_ASSERT(0);
            }
          }
          {
            GenDb::ColList *col_list(new GenDb::ColList);

            /* Table */
            col_list->cfname_ = g_viz_constants.FLOW_TABLE_PROT_SP;

            GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

            pugi::xml_node runnode;
            int32_t runint32;

            /* setup the rowkey */
            rowkey.push_back(t2);
            rowkey.push_back(dir_val);

            std::vector<GenDb::NewCol>& columns = col_list->columns_;

            GenDb::DbDataValueVec col_name;
            /* setup the column-name */
            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_PROTOCOL)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint8_t)runint32);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SPORT)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint16_t)runint32);

            /* T1 */
            col_name.push_back(t1);

            columns.push_back(GenDb::NewCol(col_name, col_value));

            std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);

            if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
                VIZD_ASSERT(0);
            }
          }
          {
            GenDb::ColList *col_list(new GenDb::ColList);

            /* Table */
            col_list->cfname_ = g_viz_constants.FLOW_TABLE_PROT_DP;

            GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

            pugi::xml_node runnode;
            int32_t runint32;

            /* setup the rowkey */
            rowkey.push_back(t2);
            rowkey.push_back(dir_val);

            std::vector<GenDb::NewCol>& columns = col_list->columns_;

            GenDb::DbDataValueVec col_name;
            /* setup the column-name */
            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_PROTOCOL)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint8_t)runint32);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DPORT)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint16_t)runint32);

            /* T1 */
            col_name.push_back(t1);

            columns.push_back(GenDb::NewCol(col_name, col_value));

            std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);

            if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
                VIZD_ASSERT(0);
            }
          }
          {
            GenDb::ColList *col_list(new GenDb::ColList);

            /* Table */
            col_list->cfname_ = g_viz_constants.FLOW_TABLE_VROUTER;

            GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

            pugi::xml_node runnode;

            /* setup the rowkey */
            rowkey.push_back(t2);
            rowkey.push_back(dir_val);

            std::vector<GenDb::NewCol>& columns = col_list->columns_;

            GenDb::DbDataValueVec col_name;
            /* setup the column-name */
            col_name.push_back(rmsg.hdr.get_Source());
            /* T1 */
            col_name.push_back(t1);

            columns.push_back(GenDb::NewCol(col_name, col_value));

            std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);

            if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
                VIZD_ASSERT(0);
            }
          }
          {
            GenDb::ColList *col_list(new GenDb::ColList);

            /* Table */
            col_list->cfname_ = g_viz_constants.FLOW_TABLE_ALL_FIELDS;

            GenDb::DbDataValueVec& rowkey = col_list->rowkey_;

            pugi::xml_node runnode;
            int32_t runint32;

            /* setup the rowkey */
            rowkey.push_back(t2);
            rowkey.push_back(dir_val);

            std::vector<GenDb::NewCol>& columns = col_list->columns_;

            GenDb::DbDataValueVec col_name;
            /* setup the column-name */
            col_name.push_back(t1);

            col_name.push_back(rmsg.hdr.get_Source());

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SOURCEVN)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            runstring = runnode.child_value();
            col_name.push_back(runstring);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DESTVN)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            runstring = runnode.child_value();
            col_name.push_back(runstring);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SOURCEIP)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint32_t)runint32);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DESTIP)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint32_t)runint32);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_PROTOCOL)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint8_t)runint32);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_SPORT)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint16_t)runint32);

            pugi_p = std::string(g_viz_constants.FlowRecordNames.find(FlowRecordFields::FLOWREC_DPORT)->second);
            runnode = parent.find_node(pugi_p);
            if (!runnode) {
                VIZD_ASSERT(0);
            }
            stringToInteger(runnode.child_value(), runint32);
            col_name.push_back((uint16_t)runint32);

            columns.push_back(GenDb::NewCol(col_name, col_value));

            std::auto_ptr<GenDb::ColList> col_list_ptr(col_list);

            if (!dbif_->NewDb_AddColumn(col_list_ptr)) {
                VIZD_ASSERT(0);
            }
          }
    }

    return true;
}
