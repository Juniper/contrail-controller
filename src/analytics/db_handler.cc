/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <exception>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/array.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <base/logging.h>
#include <io/event_manager.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_message_builder.h>

#include "viz_constants.h"
#include "vizd_table_desc.h"
#include "collector.h"
#include "db_handler.h"

#define DB_LOG(_Level, _Msg)                                                   \
    do {                                                                       \
        if (LoggingDisabled()) break;                                          \
        log4cplus::Logger _Xlogger = log4cplus::Logger::getRoot();             \
        if (_Xlogger.isEnabledFor(log4cplus::_Level##_LOG_LEVEL)) {            \
            log4cplus::tostringstream _Xbuf;                                   \
            _Xbuf << name_ << ": " << __func__ << ": " << _Msg;                \
            _Xlogger.forcedLog(log4cplus::_Level##_LOG_LEVEL,                  \
                               _Xbuf.str());                                   \
        }                                                                      \
    } while (false)

using std::string;
using boost::system::error_code;
using namespace pugi;

DbHandler::DbHandler(EventManager *evm,
        GenDb::GenDbIf::DbErrorHandler err_handler,
        std::string cassandra_ip, unsigned short cassandra_port,
        int analytics_ttl, std::string name) :
    dbif_(GenDb::GenDbIf::GenDbIfImpl(err_handler,
          cassandra_ip, cassandra_port, analytics_ttl*3600, name, false)),
    name_(name),
    drop_level_(SandeshLevel::INVALID) {
        error_code error;
        col_name_ = boost::asio::ip::host_name(error);
}

DbHandler::DbHandler(GenDb::GenDbIf *dbif) :
    dbif_(dbif) {
}

DbHandler::~DbHandler() {
}

bool DbHandler::DropMessage(const SandeshHeader &header,
    const VizMsg *vmsg) {
    bool drop(DoDropSandeshMessage(header, drop_level_));
    if (drop) {
        tbb::mutex::scoped_lock lock(smutex_);
        dropped_msg_stats_.Update(vmsg);
    }
    return drop;
}
 
void DbHandler::SetDropLevel(size_t queue_count, SandeshLevel::type level) {
    if (drop_level_ != level) {
        DB_LOG(INFO, "DB DROP LEVEL: [" << 
            Sandesh::LevelToString(drop_level_) << "] -> [" <<
            Sandesh::LevelToString(level) << "], DB QUEUE COUNT: " << 
            queue_count);
        drop_level_ = level;
    }
}

bool DbHandler::CreateTables() {
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_tables.begin();
            it != vizd_tables.end(); it++) {
        if (!dbif_->Db_AddColumnfamily(*it)) {
            DB_LOG(ERROR, it->cfname_ << " FAILED");
            return false;
        }
    }

    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_flow_tables.begin();
            it != vizd_flow_tables.end(); it++) {
        if (!dbif_->Db_AddColumnfamily(*it)) {
            DB_LOG(ERROR, it->cfname_ << " FAILED");
            return false;
        }
    }

    GenDb::ColList col_list;
    std::string cfname = g_viz_constants.SYSTEM_OBJECT_TABLE;
    GenDb::DbDataValueVec key;
    key.push_back(g_viz_constants.SYSTEM_OBJECT_ANALYTICS);

    bool init_done = false;
    if (dbif_->Db_GetRow(col_list, cfname, key)) {
        for (GenDb::NewColVec::iterator it = col_list.columns_.begin();
                it != col_list.columns_.end(); it++) {
            std::string col_name;
            try {
                col_name = boost::get<std::string>(it->name->at(0));
            } catch (boost::bad_get& ex) {
                DB_LOG(ERROR, cfname << ": Column Name Get FAILED");
            }

            if (col_name == g_viz_constants.SYSTEM_OBJECT_START_TIME) {
                init_done = true;
            }
        }
    }

    if (!init_done) {
        std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
        col_list->cfname_ = g_viz_constants.SYSTEM_OBJECT_TABLE;
        // Rowkey
        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
        rowkey.reserve(1);
        rowkey.push_back(g_viz_constants.SYSTEM_OBJECT_ANALYTICS);
        // Columns
        GenDb::NewColVec& columns = col_list->columns_;
        GenDb::NewCol *col(new GenDb::NewCol(
            g_viz_constants.SYSTEM_OBJECT_START_TIME, UTCTimestampUsec(), 0));
        columns.reserve(1);
        columns.push_back(col);
        if (!dbif_->Db_AddColumnSync(col_list)) {
            VIZD_ASSERT(0);
        }
    }

    return true;
}

void DbHandler::UnInit(int instance) {
    dbif_->Db_Uninit("analytics::DbHandler", instance);
    dbif_->Db_SetInitDone(false);
}

bool DbHandler::Init(bool initial, int instance) {
    if (initial) {
        return Initialize(instance);
    } else {
        return Setup(instance);
    }
}

bool DbHandler::Initialize(int instance) {
    DB_LOG(DEBUG, "Initializing..");

    /* init of vizd table structures */
    init_vizd_tables();

    if (!dbif_->Db_Init("analytics::DbHandler", instance)) {
        DB_LOG(ERROR, "Connection to DB FAILED");
        return false;
    }

    if (!dbif_->Db_AddSetTablespace(g_viz_constants.COLLECTOR_KEYSPACE,"2")) {
        DB_LOG(ERROR, "Create/Set KEYSPACE: " <<
            g_viz_constants.COLLECTOR_KEYSPACE << " FAILED");
        return false;
    }

    if (!CreateTables()) {
        DB_LOG(ERROR, "CreateTables FAILED");
        return false;
    }

    dbif_->Db_SetInitDone(true);
    DB_LOG(DEBUG, "Initializing Done");

    return true;
}

bool DbHandler::Setup(int instance) {
    DB_LOG(DEBUG, "Setup..");
    if (!dbif_->Db_Init("analytics::DbHandler", 
                       instance)) {
        DB_LOG(ERROR, "Connection to DB FAILED");
        return false;
    }
    if (!dbif_->Db_SetTablespace(g_viz_constants.COLLECTOR_KEYSPACE)) {
        DB_LOG(ERROR, "Set KEYSPACE: " <<
                g_viz_constants.COLLECTOR_KEYSPACE << " FAILED");
        return false;
    }   
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_tables.begin();
            it != vizd_tables.end(); it++) {
        if (!dbif_->Db_UseColumnfamily(*it)) {
            DB_LOG(ERROR, it->cfname_ << 
                   ": Db_UseColumnfamily FAILED");
            return false;
        }
    }
    /* setup ObjectTable */
    if (!dbif_->Db_UseColumnfamily(
                (GenDb::NewCf(g_viz_constants.OBJECT_TABLE,
                              boost::assign::list_of
                              (GenDb::DbDataType::Unsigned32Type)
                              (GenDb::DbDataType::Unsigned8Type)
                              (GenDb::DbDataType::AsciiType),
                              boost::assign::list_of
                              (GenDb::DbDataType::AsciiType)
                              (GenDb::DbDataType::Unsigned32Type),
                              boost::assign::list_of
                              (GenDb::DbDataType::LexicalUUIDType))))) {
        DB_LOG(ERROR, g_viz_constants.OBJECT_TABLE << 
               ": Db_UseColumnfamily FAILED");
        return false;
    }
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_flow_tables.begin();
            it != vizd_flow_tables.end(); it++) {
        if (!dbif_->Db_UseColumnfamily(*it)) {
            DB_LOG(ERROR, it->cfname_ << ": Db_UseColumnfamily FAILED");
            return false;
        }
    }
    dbif_->Db_SetInitDone(true);
    DB_LOG(DEBUG, "Setup Done");
    return true;
}

void DbHandler::SetDbQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm) {
    dbif_->Db_SetQueueWaterMark(boost::get<2>(wm),
        boost::get<0>(wm),
        boost::bind(&DbHandler::SetDropLevel, this, _1, boost::get<1>(wm)));
}

void DbHandler::ResetDbQueueWaterMarkInfo() {
    dbif_->Db_ResetQueueWaterMarks();
}

bool DbHandler::GetStats(uint64_t &queue_count, uint64_t &enqueues,
    std::string &drop_level, std::vector<SandeshStats> &vdropmstats) const {
    drop_level = Sandesh::LevelToString(drop_level_);
    {
        tbb::mutex::scoped_lock lock(smutex_);
        dropped_msg_stats_.Get(vdropmstats);
    } 
    return dbif_->Db_GetQueueStats(queue_count, enqueues);
}

bool DbHandler::GetStats(std::vector<GenDb::DbTableInfo> &vdbti,
    GenDb::DbErrors &dbe) {
    return dbif_->Db_GetStats(vdbti, dbe);
}

bool DbHandler::AllowMessageTableInsert(const SandeshHeader &header) {
    return header.get_Type() != SandeshType::FLOW;
}

bool DbHandler::MessageIndexTableInsert(const std::string& cfname,
        const SandeshHeader& header,
        const std::string& message_type,
        const boost::uuids::uuid& unm) {
    std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
    col_list->cfname_ = cfname;
    // Rowkey
    GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
    rowkey.reserve(8);
    uint32_t T2(header.get_Timestamp() >> g_viz_constants.RowTimeInBits);
    rowkey.push_back(T2);
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
        DB_LOG(ERROR, "Unknown table: " << cfname << ", message: "
                << message_type << ", message UUID: " << unm);
        return false;
    }
    // Columns
    GenDb::NewColVec& columns = col_list->columns_;
    columns.reserve(1);
    uint32_t T1(header.get_Timestamp() & g_viz_constants.RowTimeInMask);
    GenDb::DbDataValueVec *col_name(new GenDb::DbDataValueVec(1, T1));
    GenDb::DbDataValueVec *col_value(new GenDb::DbDataValueVec(1, unm));
    GenDb::NewCol *col(new GenDb::NewCol(col_name, col_value));
    columns.push_back(col);
    if (!dbif_->Db_AddColumn(col_list)) {
        DB_LOG(ERROR, "Addition of message: " << message_type <<
                ", message UUID: " << unm << " to table: " << cfname <<
                " FAILED");
        return false;
    }
    return true;
}

void DbHandler::MessageTableOnlyInsert(const VizMsg *vmsgp) {
    const SandeshHeader &header(vmsgp->msg->GetHeader());
    const std::string &message_type(vmsgp->msg->GetMessageType());
    uint64_t temp_u64;
    uint32_t temp_u32;
    std::string temp_str;

    std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
    col_list->cfname_ = g_viz_constants.COLLECTOR_GLOBAL_TABLE;
    // Rowkey
    GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
    rowkey.reserve(1);
    rowkey.push_back(vmsgp->unm);
    // Columns
    GenDb::NewColVec& columns = col_list->columns_;
    columns.reserve(16);
    columns.push_back(new GenDb::NewCol(g_viz_constants.SOURCE,
        header.get_Source()));
    columns.push_back(new GenDb::NewCol(g_viz_constants.NAMESPACE,
        header.get_Namespace()));
    columns.push_back(new GenDb::NewCol(g_viz_constants.MODULE,
        header.get_Module()));
    if (!header.get_Context().empty()) {
        columns.push_back(new GenDb::NewCol(g_viz_constants.CONTEXT,
            header.get_Context()));
    }
    if (!header.get_InstanceId().empty()) {
        columns.push_back(new GenDb::NewCol(g_viz_constants.INSTANCE_ID, 
                                            header.get_InstanceId()));
    }
    if (!header.get_NodeType().empty()) {
        columns.push_back(new GenDb::NewCol(g_viz_constants.NODE_TYPE,
                                            header.get_NodeType()));
    }
    if (header.__isset.IPAddress) {
        columns.push_back(new GenDb::NewCol(g_viz_constants.IPADDRESS,
                                            header.get_IPAddress()));
    }
    // Convert to network byte order
    temp_u64 = header.get_Timestamp();
    columns.push_back(new GenDb::NewCol(g_viz_constants.TIMESTAMP, temp_u64));

    columns.push_back(new GenDb::NewCol(g_viz_constants.CATEGORY,
        header.get_Category()));

    temp_u32 = header.get_Level();
    columns.push_back(new GenDb::NewCol(g_viz_constants.LEVEL, temp_u32));

    columns.push_back(new GenDb::NewCol(g_viz_constants.MESSAGE_TYPE,
        message_type));

    temp_u32 = header.get_SequenceNum();
    columns.push_back(new GenDb::NewCol(g_viz_constants.SEQUENCE_NUM,
        temp_u32));

    temp_u32 = header.get_VersionSig();
    columns.push_back(new GenDb::NewCol(g_viz_constants.VERSION, temp_u32));

    uint8_t temp_u8 = header.get_Type();
    columns.push_back(new GenDb::NewCol(g_viz_constants.SANDESH_TYPE,
        temp_u8));
    if (header.__isset.Pid) {
        temp_u32 = header.get_Pid();
        columns.push_back(new GenDb::NewCol(g_viz_constants.PID,
                                        temp_u32));
    }

    columns.push_back(new GenDb::NewCol(g_viz_constants.DATA,
        vmsgp->msg->ExtractMessage()));

    if (!dbif_->Db_AddColumn(col_list)) {
        DB_LOG(ERROR, "Addition of message: " << message_type <<
                ", message UUID: " << vmsgp->unm << " COLUMN FAILED");
        return;
    }
}

void DbHandler::MessageTableInsert(const VizMsg *vmsgp) {
    const SandeshHeader &header(vmsgp->msg->GetHeader());
    const std::string &message_type(vmsgp->msg->GetMessageType());

    if (!AllowMessageTableInsert(header))
        return;

    MessageTableOnlyInsert(vmsgp);

    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_SOURCE, header, message_type, vmsgp->unm);
    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_MODULE_ID, header, message_type, vmsgp->unm);
    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_CATEGORY, header, message_type, vmsgp->unm);
    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE, header, message_type, vmsgp->unm);
    MessageIndexTableInsert(g_viz_constants.MESSAGE_TABLE_TIMESTAMP, header, message_type, vmsgp->unm);
    /*
     * Insert the message types in the stat table
     * Construct the atttributes,attrib_tags beofore inserting
     * to the StatTableInsert
     */
    uint64_t temp_u64;
    DbHandler::TagMap tmap;
    DbHandler::AttribMap amap;
    DbHandler::Var pv;
    DbHandler::AttribMap attribs;
    std::string name_val = string(g_viz_constants.COLLECTOR_GLOBAL_TABLE);
    name_val.append(":Messagetype");
    pv = name_val;
    tmap.insert(make_pair("name", make_pair(pv, amap)));
    attribs.insert(make_pair(string("name"), pv));
    string sattrname("fields.value");
    pv = string(message_type);
    attribs.insert(make_pair(sattrname,pv));

    //pv = string(header.get_Source());
    // Put the name of the collector, not the message source.
    // Using the message source will make queries slower
    pv = string(col_name_);
    tmap.insert(make_pair("Source",make_pair(pv,amap))); 
    attribs.insert(make_pair(string("Source"),pv));

    temp_u64 = header.get_Timestamp();
    StatTableInsert(temp_u64, "FieldNames","fields",tmap,attribs);

}

void DbHandler::GetRuleMap(RuleMap& rulemap) {
}

/*
 * insert an entry into an ObjectTrace table
 * key is T2
 * column is
 *  name: <key>:T1 (value in timestamp)
 *  value: uuid (of the corresponding global message)
 */
void DbHandler::ObjectTableInsert(const std::string &table, const std::string &objectkey_str,
        uint64_t &timestamp, const boost::uuids::uuid& unm) {
    uint32_t T2(timestamp >> g_viz_constants.RowTimeInBits);
    uint32_t T1(timestamp & g_viz_constants.RowTimeInMask);

      {
        uint8_t partition_no = 0;
        std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
        col_list->cfname_ = g_viz_constants.OBJECT_TABLE;
        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
        rowkey.reserve(3);
        rowkey.push_back(T2);
        rowkey.push_back(partition_no);
        rowkey.push_back(table);
        
        GenDb::DbDataValueVec *col_name(new GenDb::DbDataValueVec());
        col_name->reserve(2);
        col_name->push_back(objectkey_str);
        col_name->push_back(T1);

        GenDb::DbDataValueVec *col_value(new GenDb::DbDataValueVec(1, unm));
        GenDb::NewCol *col(new GenDb::NewCol(col_name, col_value));
        GenDb::NewColVec& columns = col_list->columns_;
        columns.reserve(1);
        columns.push_back(col);
        if (!dbif_->Db_AddColumn(col_list)) {
            DB_LOG(ERROR, "Addition of " << objectkey_str <<
                    ", message UUID " << unm << " into table " << table <<
                    " FAILED");
            return;
        }
      }

      {
        std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
        col_list->cfname_ = g_viz_constants.OBJECT_VALUE_TABLE;
        GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
        rowkey.reserve(2);
        rowkey.push_back(T2);
        rowkey.push_back(table);
        GenDb::DbDataValueVec *col_name(new GenDb::DbDataValueVec(1, T1));
        GenDb::DbDataValueVec *col_value(new GenDb::DbDataValueVec(1, objectkey_str));
        GenDb::NewCol *col(new GenDb::NewCol(col_name, col_value));
        GenDb::NewColVec& columns = col_list->columns_;
        columns.reserve(1);
        columns.push_back(col);
        if (!dbif_->Db_AddColumn(col_list)) {
            DB_LOG(ERROR, "Addition of " << objectkey_str <<
                    ", message UUID " << unm << " " << table << " into table "
                    << g_viz_constants.OBJECT_VALUE_TABLE << " FAILED");
            return;
        }

	/*
	 * Inserting into the stat table
	 */
	DbHandler::TagMap tmap;
	DbHandler::AttribMap amap;
	DbHandler::Var pv;
	DbHandler::AttribMap attribs;
	std::string name_val = string(table);
	name_val.append(":Objecttype");
	pv = name_val;
	tmap.insert(make_pair("name", make_pair(pv, amap)));
	attribs.insert(make_pair(string("name"), pv));
	string sattrname("fields.value");
	pv = string(objectkey_str);
	attribs.insert(make_pair(sattrname,pv));
        
        pv = string(col_name_);
        tmap.insert(make_pair("Source",make_pair(pv,amap)));
        attribs.insert(make_pair(string("Source"),pv));

	StatTableInsert(timestamp, "FieldNames","fields",tmap,attribs);
    }
        
}


void DbHandler::StatTableInsert(uint64_t ts, 
        const std::string& statName,
        const std::string& statAttr,
        const TagMap & attribs_tag,
        const AttribMap & attribs) {

    uint64_t temp_u64 = ts;
    uint32_t temp_u32 = temp_u64 >> g_viz_constants.RowTimeInBits;
    boost::uuids::uuid unm;
    if (statName.compare("FieldNames") == 0) {
         //the unm should be fff.. for all UUID
         boost::uuids::string_generator gen;
	 unm = gen(std::string("ffffffffffffffffffffffffffffffff"));
    } else {
         unm = umn_gen_();
    }

    // This is very primitive JSON encoding.
    // Should replace with rapidJson at some point.

    // Encoding of all attribs

    rapidjson::Document dd;
    dd.SetObject();

    for (AttribMap::const_iterator it = attribs.begin();
            it != attribs.end(); it++) {
        switch (it->second.type) {
            case STRING: {
                    rapidjson::Value val(rapidjson::kStringType);
                    val.SetString(it->second.str.c_str());
                    dd.AddMember(it->first.c_str(), val, dd.GetAllocator());
                }
                break;
            case UINT64: {
                    rapidjson::Value val(rapidjson::kNumberType);
                    val.SetUint64(it->second.num);
                    dd.AddMember(it->first.c_str(), val, dd.GetAllocator());
                }
                break;
            case DOUBLE: {
                    rapidjson::Value val(rapidjson::kNumberType);
                    val.SetDouble(it->second.dbl);
                    dd.AddMember(it->first.c_str(), val, dd.GetAllocator());                    
                }
                break;                
            default:
                assert(0);                                
        }
    }

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    dd.Accept(writer);
    string jsonline(sb.GetString());

    for (TagMap::const_iterator it = attribs_tag.begin();
            it != attribs_tag.end(); it++) {
        if (it->second.second.empty()) {

            std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);

            GenDb::DbDataValue pv;
            std::string cfname;

            if (it->second.first.type == UINT64) {
                cfname = col_list->cfname_ = 
                    g_viz_constants.STATS_TABLE_BY_U64_STR_TAG;
                pv = it->second.first.num;
            } else if (it->second.first.type == DOUBLE) {
                cfname = col_list->cfname_ = 
                    g_viz_constants.STATS_TABLE_BY_DBL_STR_TAG;
                pv = it->second.first.dbl;
            } else {
                cfname = col_list->cfname_ = 
                    g_viz_constants.STATS_TABLE_BY_STR_STR_TAG;
                pv = it->second.first.str;
            }

            GenDb::DbDataValueVec& rowkey = col_list->rowkey_;
            rowkey.reserve(4);
            rowkey.push_back(temp_u32);
            rowkey.push_back(statName);
            rowkey.push_back(statAttr);
            rowkey.push_back(it->first);

            GenDb::NewColVec& columns = col_list->columns_;

            GenDb::DbDataValueVec *col_name(new GenDb::DbDataValueVec);
            col_name->reserve(4);
            col_name->push_back(pv);
            col_name->push_back(string());
            if ( statName.compare("FieldNames") != 0) {
                col_name->push_back((uint32_t)
                    (temp_u64& g_viz_constants.RowTimeInMask));
            } else {
		//Make t2 0 for 
                col_name->push_back((uint32_t)0);
            }
	    
            col_name->push_back(unm);
            GenDb::DbDataValueVec *col_value(new GenDb::DbDataValueVec(1, jsonline));
            GenDb::NewCol *col(new GenDb::NewCol(col_name, col_value));
            columns.push_back(col);

            if (!dbif_->Db_AddColumn(col_list)) {
                DB_LOG(ERROR, "Addition of " << statName <<
                        ", " << statAttr << " attrib " << it->first << " into table "
                        << cfname << " FAILED");
            }

        } else {
            // TODO: add support for suffix tagging
            assert(0);
        }
    }

}

typedef boost::array<GenDb::DbDataValue,
    FlowRecordFields::FLOWREC_MAX> FlowValueArray;

static const std::vector<FlowRecordFields::type> FlowRecordTableColumns =
    boost::assign::list_of
    (FlowRecordFields::FLOWREC_VROUTER)
    (FlowRecordFields::FLOWREC_DIRECTION_ING)
    (FlowRecordFields::FLOWREC_SOURCEVN)
    (FlowRecordFields::FLOWREC_SOURCEIP)
    (FlowRecordFields::FLOWREC_DESTVN)
    (FlowRecordFields::FLOWREC_DESTIP)
    (FlowRecordFields::FLOWREC_PROTOCOL)
    (FlowRecordFields::FLOWREC_SPORT)
    (FlowRecordFields::FLOWREC_DPORT)
    (FlowRecordFields::FLOWREC_TOS)
    (FlowRecordFields::FLOWREC_TCP_FLAGS)
    (FlowRecordFields::FLOWREC_VM)
    (FlowRecordFields::FLOWREC_INPUT_INTERFACE)
    (FlowRecordFields::FLOWREC_OUTPUT_INTERFACE)
    (FlowRecordFields::FLOWREC_MPLS_LABEL)
    (FlowRecordFields::FLOWREC_REVERSE_UUID)
    (FlowRecordFields::FLOWREC_SETUP_TIME)
    (FlowRecordFields::FLOWREC_TEARDOWN_TIME)
    (FlowRecordFields::FLOWREC_MIN_INTERARRIVAL)
    (FlowRecordFields::FLOWREC_MAX_INTERARRIVAL)
    (FlowRecordFields::FLOWREC_MEAN_INTERARRIVAL)
    (FlowRecordFields::FLOWREC_STDDEV_INTERARRIVAL)
    (FlowRecordFields::FLOWREC_BYTES)
    (FlowRecordFields::FLOWREC_PACKETS)
    (FlowRecordFields::FLOWREC_DATA_SAMPLE)
    (FlowRecordFields::FLOWREC_ACTION);

static void PopulateFlowRecordTableColumns(
    const std::vector<FlowRecordFields::type> &frvt,
    FlowValueArray &fvalues, GenDb::NewColVec& columns) {
    columns.reserve(frvt.size());
    for (std::vector<FlowRecordFields::type>::const_iterator it = frvt.begin();
         it != frvt.end(); it++) {
        GenDb::DbDataValue &db_value(fvalues[(*it)]);
        if (db_value.which() != GenDb::DB_VALUE_BLANK) {
            GenDb::NewCol *col(new GenDb::NewCol(
                g_viz_constants.FlowRecordNames[(*it)], db_value));
            columns.push_back(col);
        }
    }
}

// FLOW_UUID
static void PopulateFlowRecordTableRowKey(
    FlowValueArray &fvalues, GenDb::DbDataValueVec &rkey) {
    rkey.reserve(1);
    GenDb::DbDataValue &flowu(fvalues[FlowRecordFields::FLOWREC_FLOWUUID]);
    assert(flowu.which() != GenDb::DB_VALUE_BLANK);
    rkey.push_back(flowu);
}

static bool PopulateFlowRecordTable(FlowValueArray &fvalues,
    GenDb::GenDbIf *dbif) {
    std::auto_ptr<GenDb::ColList> colList(new GenDb::ColList);
    colList->cfname_ = g_viz_constants.FLOW_TABLE;
    PopulateFlowRecordTableRowKey(fvalues, colList->rowkey_);
    PopulateFlowRecordTableColumns(FlowRecordTableColumns, fvalues,
        colList->columns_);
    return dbif->Db_AddColumn(colList);
}

static const std::vector<FlowRecordFields::type> FlowIndexTableColumnValues =
    boost::assign::list_of
    (FlowRecordFields::FLOWREC_DIFF_BYTES)
    (FlowRecordFields::FLOWREC_DIFF_PACKETS)
    (FlowRecordFields::FLOWREC_SHORT_FLOW)
    (FlowRecordFields::FLOWREC_FLOWUUID)
    (FlowRecordFields::FLOWREC_VROUTER)
    (FlowRecordFields::FLOWREC_SOURCEVN)
    (FlowRecordFields::FLOWREC_DESTVN)
    (FlowRecordFields::FLOWREC_SOURCEIP)
    (FlowRecordFields::FLOWREC_DESTIP)
    (FlowRecordFields::FLOWREC_PROTOCOL)
    (FlowRecordFields::FLOWREC_SPORT)
    (FlowRecordFields::FLOWREC_DPORT)
    (FlowRecordFields::FLOWREC_JSON);

enum FlowIndexTableType {
    FLOW_INDEX_TABLE_MIN,
    FLOW_INDEX_TABLE_SVN_SIP = FLOW_INDEX_TABLE_MIN,
    FLOW_INDEX_TABLE_DVN_DIP,
    FLOW_INDEX_TABLE_PROTOCOL_SPORT,
    FLOW_INDEX_TABLE_PROTOCOL_DPORT,
    FLOW_INDEX_TABLE_VROUTER,
    FLOW_INDEX_TABLE_MAX_PLUS_1,
};

static const std::string& FlowIndexTable2String(FlowIndexTableType ttype) {
    switch (ttype) {
    case FLOW_INDEX_TABLE_SVN_SIP:
        return g_viz_constants.FLOW_TABLE_SVN_SIP;
    case FLOW_INDEX_TABLE_DVN_DIP:
        return g_viz_constants.FLOW_TABLE_DVN_DIP;
    case FLOW_INDEX_TABLE_PROTOCOL_SPORT:
        return g_viz_constants.FLOW_TABLE_PROT_SP;
    case FLOW_INDEX_TABLE_PROTOCOL_DPORT:
        return g_viz_constants.FLOW_TABLE_PROT_DP;
    case FLOW_INDEX_TABLE_VROUTER:
        return g_viz_constants.FLOW_TABLE_VROUTER;
    default:
        return g_viz_constants.FLOW_TABLE_INVALID;
    }
}
 
static void PopulateFlowIndexTableColumnValues(
    const std::vector<FlowRecordFields::type> &frvt,
    FlowValueArray &fvalues, GenDb::DbDataValueVec &cvalues) {
    cvalues.reserve(frvt.size());
    for (std::vector<FlowRecordFields::type>::const_iterator it = frvt.begin();
         it != frvt.end(); it++) {
        GenDb::DbDataValue &db_value(fvalues[(*it)]);
        if (db_value.which() != GenDb::DB_VALUE_BLANK) {
            cvalues.push_back(db_value);
        }
    }
}

// T2, Partition No, Direction
static void PopulateFlowIndexTableRowKey(
    FlowValueArray &fvalues, uint32_t &T2, uint8_t &partition_no,
    GenDb::DbDataValueVec &rkey) {
    rkey.reserve(3);
    rkey.push_back(T2);
    rkey.push_back(partition_no);
    rkey.push_back(fvalues[FlowRecordFields::FLOWREC_DIRECTION_ING]);
}

// SVN/DVN/Protocol, SIP/DIP/SPORT/DPORT, T1, FLOW_UUID
static void PopulateFlowIndexTableColumnNames(FlowIndexTableType ftype,
    FlowValueArray &fvalues, uint32_t &T1,
    GenDb::DbDataValueVec *cnames) {
    cnames->reserve(4);
    switch(ftype) {
    case FLOW_INDEX_TABLE_SVN_SIP:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_SOURCEVN]);
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_SOURCEIP]);
        break;
    case FLOW_INDEX_TABLE_DVN_DIP:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_DESTVN]);
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_DESTIP]);
        break;
    case FLOW_INDEX_TABLE_PROTOCOL_SPORT:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_PROTOCOL]);
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_SPORT]);
        break;
    case FLOW_INDEX_TABLE_PROTOCOL_DPORT:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_PROTOCOL]);
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_DPORT]);
        break;
    case FLOW_INDEX_TABLE_VROUTER:
        cnames->push_back(fvalues[FlowRecordFields::FLOWREC_VROUTER]);
        break;
    default:
        VIZD_ASSERT(0);
        break;
    }
    cnames->push_back(T1);
    cnames->push_back(fvalues[FlowRecordFields::FLOWREC_FLOWUUID]);
}

static void PopulateFlowIndexTableColumns(FlowIndexTableType ftype,
    FlowValueArray &fvalues, uint32_t &T1,
    GenDb::NewColVec &columns, const GenDb::DbDataValueVec &cvalues) {
    GenDb::DbDataValueVec *names(new GenDb::DbDataValueVec);
    PopulateFlowIndexTableColumnNames(ftype, fvalues, T1, names);
    GenDb::DbDataValueVec *values(new GenDb::DbDataValueVec(cvalues));
    GenDb::NewCol *col(new GenDb::NewCol(names, values));
    columns.reserve(1);
    columns.push_back(col);
}

static bool PopulateFlowIndexTables(FlowValueArray &fvalues, 
    uint32_t &T2, uint32_t &T1, uint8_t partition_no,
    GenDb::GenDbIf *dbif) {
    // Populate row key and column values (same for all flow index
    // tables)
    GenDb::DbDataValueVec rkey;
    PopulateFlowIndexTableRowKey(fvalues, T2, partition_no, rkey);
    GenDb::DbDataValueVec cvalues;
    PopulateFlowIndexTableColumnValues(FlowIndexTableColumnValues, fvalues,
        cvalues);
    // Populate the Flow Index Tables
    for (int tid = FLOW_INDEX_TABLE_MIN;
         tid < FLOW_INDEX_TABLE_MAX_PLUS_1; ++tid) {
        FlowIndexTableType fitt(static_cast<FlowIndexTableType>(tid));
        std::auto_ptr<GenDb::ColList> colList(new GenDb::ColList);
        colList->cfname_ = FlowIndexTable2String(fitt);
        colList->rowkey_ = rkey;
        PopulateFlowIndexTableColumns(fitt, fvalues, T1, colList->columns_,
            cvalues);
        if (!dbif->Db_AddColumn(colList)) {
            LOG(ERROR, "Populating " << FlowIndexTable2String(fitt) <<
                " FAILED");
        }
    }
    return true;
}

template <typename T>
bool FlowDataIpv4ObjectWalker<T>::for_each(pugi::xml_node& node) {
    std::string col_name(node.name());
    FlowTypeMap::const_iterator it = flow_msg2type_map.find(col_name);
    if (it != flow_msg2type_map.end()) {
        // Extract the values and populate the value array
        const FlowTypeInfo &ftinfo(it->second);
        switch (ftinfo.get<1>()) {
        case GenDb::DbDataType::Unsigned8Type:
            {
                int8_t val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = static_cast<uint8_t>(val);
                break;
            }
        case GenDb::DbDataType::Unsigned16Type:
            {
                int16_t val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = static_cast<uint16_t>(val);
                break;
            }
        case GenDb::DbDataType::Unsigned32Type:
            {
                int32_t val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = static_cast<uint32_t>(val);
                break;
            }
        case GenDb::DbDataType::Unsigned64Type:
            {
                int64_t val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = static_cast<uint64_t>(val);
                break;
            }
        case GenDb::DbDataType::DoubleType:
            {
                double val;
                stringToInteger(node.child_value(), val);
                values_[ftinfo.get<0>()] = val;
                break;
            }
        case GenDb::DbDataType::LexicalUUIDType:
        case GenDb::DbDataType::TimeUUIDType:
            {
                values_[ftinfo.get<0>()] = s_gen_(node.child_value());
                break;
            }
        case GenDb::DbDataType::AsciiType:
            {
                values_[ftinfo.get<0>()] = node.child_value();
                break;
            }
        default:
            VIZD_ASSERT(0);
            break;
        }
    }
    return true;
}

/*
 * process the flow message and insert into appropriate tables
 */
bool DbHandler::FlowTableInsert(const pugi::xml_node &parent,
    const SandeshHeader& header) {
    // Traverse and populate the flow entry values
    FlowValueArray flow_entry_values;
    FlowDataIpv4ObjectWalker<FlowValueArray> flow_msg_walker(flow_entry_values,
        s_gen_);
    pugi::xml_node &mnode = const_cast<pugi::xml_node &>(parent);
    if (!mnode.traverse(flow_msg_walker)) {
        VIZD_ASSERT(0);
    }
    // Populate FLOWREC_VROUTER from SandeshHeader source
    flow_entry_values[FlowRecordFields::FLOWREC_VROUTER] = header.get_Source();
    // Populate FLOWREC_JSON to empty string
    flow_entry_values[FlowRecordFields::FLOWREC_JSON] = std::string();
    // Populate FLOWREC_SHORT_FLOW based on setup_time and teardown_time
    GenDb::DbDataValue &setup_time(
        flow_entry_values[FlowRecordFields::FLOWREC_SETUP_TIME]);
    GenDb::DbDataValue &teardown_time(
        flow_entry_values[FlowRecordFields::FLOWREC_TEARDOWN_TIME]);
    if (setup_time.which() != GenDb::DB_VALUE_BLANK &&
        teardown_time.which() != GenDb::DB_VALUE_BLANK) {
        flow_entry_values[FlowRecordFields::FLOWREC_SHORT_FLOW] = static_cast<uint8_t>(1);
    } else {
        flow_entry_values[FlowRecordFields::FLOWREC_SHORT_FLOW] = static_cast<uint8_t>(0);
    }
    // Calculate T1 and T2 values from timestamp
    uint64_t timestamp(header.get_Timestamp());
    uint32_t T2(timestamp >> g_viz_constants.RowTimeInBits);
    uint32_t T1(timestamp & g_viz_constants.RowTimeInMask);
    // Parittion no
    uint8_t partition_no = 0;
    // Populate Flow Record Table
    if (!PopulateFlowRecordTable(flow_entry_values, dbif_.get())) {
        DB_LOG(ERROR, "Populating FlowRecordTable FAILED");
    }
    // Populate Flow Index Tables only if FLOWREC_DIFF_BYTES and
    GenDb::DbDataValue &diff_bytes(
        flow_entry_values[FlowRecordFields::FLOWREC_DIFF_BYTES]);
    GenDb::DbDataValue &diff_packets(
        flow_entry_values[FlowRecordFields::FLOWREC_DIFF_PACKETS]);
    // FLOWREC_DIFF_PACKETS are present
    if (diff_bytes.which() != GenDb::DB_VALUE_BLANK &&
        diff_packets.which() != GenDb::DB_VALUE_BLANK) {
       if (!PopulateFlowIndexTables(flow_entry_values, T2, T1, partition_no,
                dbif_.get())) {
           DB_LOG(ERROR, "Populating FlowIndexTables FAILED");
       }
    }
    return true;
}
