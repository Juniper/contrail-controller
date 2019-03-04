/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_json_parser.h"

#include <boost/lexical_cast.hpp>
#include <sandesh/request_pipeline.h>
#include <string>

#include "config-client-mgr/config_client_manager.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_sandesh_context.h"
#include "ifmap/ifmap_server_show_types.h"
#include "base/autogen_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "config-client-mgr/config_client_show_types.h"
#include "config-client-mgr/config_client_log_types.h"
#include "config-client-mgr/config_cass2json_adapter.h"
#include "config-client-mgr/config_amqp_client.h"
#include "config-client-mgr/config_db_client.h"

using contrail_rapidjson::Value;
using std::cout;
using std::endl;
using std::string;

#define CONFIG_PARSE_ASSERT(t, condition, key, value)                          \
    do {                                                                       \
        if (condition)                                                         \
            break;                                                             \
        IFMAP_WARN_LOG(ConfigurationMalformed ## t ## Warning ## Log,          \
                       Category::IFMAP, key, value, adapter.type(),            \
                       adapter.uuid());                                        \
        IFMAP_TRACE(ConfigurationMalformed ## t ## Warning ## Trace,           \
                    key, value, adapter.type(), adapter.uuid());               \
        if (ConfigCass2JsonAdapter::assert_on_parse_error())                   \
            assert(false);                                                     \
        return false;                                                          \
    } while (false)

ConfigJsonParser::ConfigJsonParser() {
}

ConfigJsonParser::~ConfigJsonParser() {
}

void ConfigJsonParser::SetupObjectFilter() {
    ObjectTypeList FilterList;
    bgp_schema_Server_GenerateObjectTypeList(&FilterList);
    vnc_cfg_Server_GenerateObjectTypeList(&FilterList);
    for (ObjectTypeList::iterator it = FilterList.begin();
        it != FilterList.end(); it++) {
        AddObjectType(*it);
    }
}

void ConfigJsonParser::SetupSchemaGraphFilter(){
    vnc_cfg_FilterInfo vnc_filter_info;
    bgp_schema_FilterInfo bgp_schema_filter_info;

    bgp_schema_Server_GenerateGraphFilter(&bgp_schema_filter_info);
    vnc_cfg_Server_GenerateGraphFilter(&vnc_filter_info);

    for (vnc_cfg_FilterInfo::iterator it = vnc_filter_info.begin();
         it != vnc_filter_info.end(); it++) {
        if (it->is_ref_) {
            AddLinkName(make_pair(it->left_, it->right_),
                            make_pair(it->metadata_, it->linkattr_));
        } else {
            AddParentName(make_pair(it->left_, it->right_),
                                              it->metadata_);
        }
    }

    for (bgp_schema_FilterInfo::iterator it = bgp_schema_filter_info.begin();
         it != bgp_schema_filter_info.end(); it++) {
        if (it->is_ref_) {
            AddLinkName(make_pair(it->left_, it->right_),
                             make_pair(it->metadata_, it->linkattr_));
        } else {
            AddParentName(make_pair(it->left_, it->right_),
                                              it->metadata_);
        }
    }
}

void ConfigJsonParser::SetupSchemaWrapperPropertyInfo() {
    WrapperFieldMap wrapper_field_map;
    bgp_schema_Server_GenerateWrapperPropertyInfo(&wrapper_field_map);
    vnc_cfg_Server_GenerateWrapperPropertyInfo(&wrapper_field_map);
    for (WrapperFieldMap::iterator it = wrapper_field_map.begin();
        it != wrapper_field_map.end(); it++) {
        AddWrapperField(it->first, it->second);
    }
}

void ConfigJsonParser::SetupGraphFilter() {
    SetupObjectFilter();
    SetupSchemaGraphFilter();
    SetupSchemaWrapperPropertyInfo();
}
void ConfigJsonParser::EndOfConfig() {
    ifmap_server_->CleanupStaleEntries();
}

void ConfigJsonParser::MetadataRegister(const string &metadata,
                                        MetadataParseFn parser) {
    pair<MetadataParseMap::iterator, bool> result =
            metadata_map_.insert(make_pair(metadata, parser));
    assert(result.second);
}

void ConfigJsonParser::MetadataClear(const string &module) {
    metadata_map_.clear();
}

IFMapTable::RequestKey *ConfigJsonParser::CloneKey(
        const IFMapTable::RequestKey &src) const {
    IFMapTable::RequestKey *retkey = new IFMapTable::RequestKey();
    retkey->id_type = src.id_type;
    retkey->id_name = src.id_name;
    // Tag each DB Request with current generation number
    retkey->id_seq_num = GetGenerationNumber();
    return retkey;
}

bool ConfigJsonParser::ParseNameType(const ConfigCass2JsonAdapter &adapter,
                                     IFMapTable::RequestKey *key) const {
    // Type is the name of the document.
    Value::ConstMemberIterator itr = adapter.document().MemberBegin();
    CONFIG_PARSE_ASSERT(Type, autogen::ParseString(itr->name, &key->id_type),
                        "Name", "Bad name");

    key->id_type = itr->name.GetString();

    // Name is the fq_name field in the document.
    const Value &value_node = itr->value;
    CONFIG_PARSE_ASSERT(FqName, value_node.HasMember("fq_name"), key->id_type,
                        "Missing FQ name");
    const Value &fq_name_node = value_node["fq_name"];
    CONFIG_PARSE_ASSERT(FqName, fq_name_node.IsArray(), key->id_type,
                        "FQ name is not an array");
    CONFIG_PARSE_ASSERT(FqName, fq_name_node.Size(),
                        key->id_type, "FQ name array is empty");

    size_t i = 0;

    // Iterate over all items except the last one.
    for (; i < fq_name_node.Size() - 1; ++i) {
        key->id_name += fq_name_node[i].GetString();
        key->id_name += string(":");
    }
    key->id_name += fq_name_node[i].GetString();

    return true;
}

bool ConfigJsonParser::ParseOneProperty(const ConfigCass2JsonAdapter &adapter,
        const Value &key_node, const Value &value_node,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list, bool add_change) const {
    string metaname = key_node.GetString();
    MetadataParseMap::const_iterator loc = metadata_map_.find(metaname);
    if (loc == metadata_map_.end()) {
        return true;
    }

    // Treat updates with NULL value as deletes.
    if (add_change && value_node.IsNull())
        add_change = false;
    auto_ptr<AutogenProperty> pvalue;
    if (add_change) {
        bool success = (loc->second)(value_node, &pvalue);
        CONFIG_PARSE_ASSERT(Property, success, metaname,
                        "No entry in metadata map");
    } else {
        const string key = metaname;
        if (!IsListOrMapPropEmpty(adapter.uuid(), key)) {
            return true;
        }
    }
    std::replace(metaname.begin(), metaname.end(), '_', '-');
    InsertRequestIntoQ(origin, "", "", metaname, pvalue, key,
                                     add_change, req_list);
    return true;
}

bool ConfigJsonParser::ParseProperties(const ConfigCass2JsonAdapter &adapter,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list, bool add_change) const {

    Value::ConstMemberIterator doc_itr = adapter.document().MemberBegin();
    const Value &value_node = doc_itr->value;
    for (Value::ConstMemberIterator itr = value_node.MemberBegin();
         itr != value_node.MemberEnd(); ++itr) {
        ParseOneProperty(adapter, itr->name, itr->value, key, origin,
                         req_list, add_change);
    }

    return true;
}

bool ConfigJsonParser::ParseRef(const ConfigCass2JsonAdapter &adapter,
        const Value &ref_entry, IFMapOrigin::Origin origin,
        const string &refer, const IFMapTable::RequestKey &key,
        RequestList *req_list, bool add_change) const {
    const Value& to_node = ref_entry["to"];

    string from_underscore = key.id_type;
    std::replace(from_underscore.begin(), from_underscore.end(), '-', '_');
    string link_name =
        GetLinkName(from_underscore, refer);
    CONFIG_PARSE_ASSERT(Reference, !link_name.empty(), refer,
                        "Link name is empty");
    string metaname = link_name;
    std::replace(metaname.begin(), metaname.end(), '-', '_');

    MetadataParseMap::const_iterator loc = metadata_map_.find(metaname);
    CONFIG_PARSE_ASSERT(Reference, loc != metadata_map_.end(), metaname,
                        "No entry in metadata map");

    auto_ptr<AutogenProperty> pvalue;
    if (ref_entry.HasMember("attr")) {
        const Value& attr_node = ref_entry["attr"];
        bool success = (loc->second)(attr_node, &pvalue);
        CONFIG_PARSE_ASSERT(ReferenceLinkAttributes, success, metaname,
                            "Link attribute parse error");
    }

    string neigh_name;
    neigh_name += to_node.GetString();

    InsertRequestIntoQ(origin, refer, neigh_name,
                                 link_name, pvalue, key, add_change, req_list);

    return true;
}

bool ConfigJsonParser::ParseOneRef(const ConfigCass2JsonAdapter &adapter,
        const Value &arr, const IFMapTable::RequestKey &key,
        IFMapOrigin::Origin origin, RequestList *req_list,
        const string &key_str, size_t pos, bool add_change) const {
    string refer = key_str.substr(0, pos);
    CONFIG_PARSE_ASSERT(Reference, arr.IsArray(), refer, "Invalid referene");
    for (size_t i = 0; i < arr.Size(); ++i)
        ParseRef(adapter, arr[i], origin, refer, key, req_list, add_change);
    return true;
}

bool ConfigJsonParser::ParseLinks(const ConfigCass2JsonAdapter &adapter,
        const IFMapTable::RequestKey &key, IFMapOrigin::Origin origin,
        RequestList *req_list, bool add_change) const {
    Value::ConstMemberIterator doc_itr = adapter.document().MemberBegin();
    const Value &properties = doc_itr->value;
    for (Value::ConstMemberIterator itr = properties.MemberBegin();
         itr != properties.MemberEnd(); ++itr) {
        string key_str = itr->name.GetString();
        // Skip all the back-refs.
        if (key_str.find("back_refs") != string::npos) {
            continue;
        }
        size_t pos = key_str.find("_refs");
        if (pos != string::npos) {
            ParseOneRef(adapter, itr->value, key, origin, req_list, key_str,
                        pos, add_change);
            continue;
        }
        if (key_str.compare("parent_type") == 0) {
            const Value& ptype_node = itr->value;
            CONFIG_PARSE_ASSERT(Parent, ptype_node.IsString(), key_str,
                                "Invalid parent type");
            pos = key.id_name.find_last_of(":");
            if (pos != string::npos) {
                string parent_type = ptype_node.GetString();
                // Get the parent name from our name.
                string parent_name = key.id_name.substr(0, pos);
                string metaname =
                    GetParentName(parent_type,key.id_type);
                CONFIG_PARSE_ASSERT(Parent, !metaname.empty(), parent_type,
                                    "Missing link name");
                auto_ptr<AutogenProperty > pvalue;
                InsertRequestIntoQ(origin, parent_type,
                     parent_name, metaname, pvalue, key, add_change, req_list);
            } else {
                continue;
            }
        }
    }

    return true;
}

bool ConfigJsonParser::ParseDocument(const ConfigCass2JsonAdapter &adapter,
        IFMapOrigin::Origin origin, RequestList *req_list,
        IFMapTable::RequestKey *key, bool add_change) const {
    // Update the name and the type into 'key'.
    if (!ParseNameType(adapter, key)) {
        return false;
    }

    // For each property, we will clone 'key' to create our DBRequest's i.e.
    // 'key' will never become part of any DBRequest.
    if (!ParseProperties(adapter, *key, origin, req_list, add_change)){
        return false;
    }

    if (!ParseLinks(adapter, *key, origin, req_list, add_change)) {
        return false;
    }

    return true;
}

void ConfigJsonParser::InsertRequestIntoQ(IFMapOrigin::Origin origin,
        const string &neigh_type, const string &neigh_name,
        const string &metaname, auto_ptr<AutogenProperty > pvalue,
        const IFMapTable::RequestKey &key, bool add_change,
        RequestList *req_list) const {

    IFMapServerTable::RequestData *data =
        new IFMapServerTable::RequestData(origin, neigh_type, neigh_name);
    data->metadata = metaname;
    data->content.reset(pvalue.release());

    DBRequest *db_request = new DBRequest();
    db_request->oper = (add_change ? DBRequest::DB_ENTRY_ADD_CHANGE :
                        DBRequest::DB_ENTRY_DELETE);
    db_request->key.reset(CloneKey(key));
    db_request->data.reset(data);

    req_list->push_back(db_request);
}

void ConfigJsonParser::EnqueueListToTables(RequestList *req_list) const {
    while (!req_list->empty()) {
        auto_ptr<DBRequest> req(req_list->front());
        req_list->pop_front();
        IFMapTable::RequestKey *key =
            static_cast<IFMapTable::RequestKey *>(req->key.get());

        IFMapTable *table = IFMapTable::FindTable(ifmap_server_->database(),
                                                  key->id_type);
        if (table != NULL) {
            table->Enqueue(req.get());
        } else {
            IFMAP_TRACE(IFMapTblNotFoundTrace, "Cant find table", key->id_type);
        }
    }
}

bool ConfigJsonParser::Receive(const ConfigCass2JsonAdapter &adapter,
                               bool add_change) {
    RequestList req_list;

    if (adapter.document().HasParseError() || !adapter.document().IsObject()) {
        size_t pos = adapter.document().GetErrorOffset();
        // GetParseError returns const char *
        IFMAP_WARN(IFMapJsonLoadError,
                   "Error in parsing JSON message at position",
                   pos, "with error description",
                   boost::lexical_cast<string>(
                       adapter.document().GetParseError()), adapter.uuid());
        return false;
    } else {
        auto_ptr<IFMapTable::RequestKey> key(new IFMapTable::RequestKey());
        if (!ParseDocument(adapter, IFMapOrigin::CASSANDRA, &req_list, key.get(), add_change)) {
            STLDeleteValues(&req_list);
            return false;
        }
        EnqueueListToTables(&req_list);
    }
    return true;
}

static bool ConfigClientInfoHandleRequest(const Sandesh *sr,
                                         const RequestPipeline::PipeSpec ps,
                                         int stage, int instNum,
                                         RequestPipeline::InstData *data) {
    const ConfigClientInfoReq *request =
        static_cast<const ConfigClientInfoReq *>(ps.snhRequest_.get());
    ConfigClientInfoResp *response = new ConfigClientInfoResp();
    IFMapSandeshContext *sctx =
        static_cast<IFMapSandeshContext *>(request->module_context("IFMap"));

    ConfigClientManager *config_mgr =
        sctx->ifmap_server()->get_config_manager();

    if (config_mgr->config_amqp_client()) {
        ConfigAmqpConnInfo amqp_conn_info;
        config_mgr->config_amqp_client()->GetConnectionInfo(amqp_conn_info);
        response->set_amqp_conn_info(amqp_conn_info);
    }

    ConfigDBConnInfo db_conn_info;
    config_mgr->config_db_client()->GetConnectionInfo(db_conn_info);

    ConfigClientManagerInfo client_mgr_info;
    config_mgr->GetClientManagerInfo(client_mgr_info);

    response->set_client_manager_info(client_mgr_info);
    response->set_db_conn_info(db_conn_info);
    response->set_context(request->context());
    response->set_more(false);
    response->Response();
    return true;
}

void ConfigClientInfoReq::HandleRequest() const {
    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("config::SandeshCmd");
    s0.cbFn_ = ConfigClientInfoHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= boost::assign::list_of(s0).convert_to_container<
                              std::vector<RequestPipeline::StageSpec> >();
    RequestPipeline rp(ps);
}

static bool ConfigClientReinitHandleRequest(const Sandesh *sr,
                                         const RequestPipeline::PipeSpec ps,
                                         int stage, int instNum,
                                         RequestPipeline::InstData *data) {
    const ConfigClientReinitReq *request =
        static_cast<const ConfigClientReinitReq *>(ps.snhRequest_.get());
    ConfigClientReinitResp *response = new ConfigClientReinitResp();
    IFMapSandeshContext *sctx =
        static_cast<IFMapSandeshContext *>(request->module_context("IFMap"));

    ConfigClientManager *config_mgr =
        sctx->ifmap_server()->get_config_manager();

    config_mgr->ReinitConfigClient();

    response->set_success(true);
    response->set_context(request->context());
    response->set_more(false);
    response->Response();
    return true;
}

void ConfigClientReinitReq::HandleRequest() const {
    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("config::SandeshCmd");
    s0.cbFn_ = ConfigClientReinitHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= boost::assign::list_of(s0).convert_to_container<
                              std::vector<RequestPipeline::StageSpec> >();
    RequestPipeline rp(ps);
}
