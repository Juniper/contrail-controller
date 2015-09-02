/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include "ifmap/ifmap_server_parser.h"

#include <pugixml/pugixml.hpp>
#include "db/db.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

using namespace std;
using namespace pugi;

IFMapServerParser::ModuleMap IFMapServerParser::module_map_;

static const char *NodeName(const xml_node &node) {
    const char *name = node.name();
    // strip namespace
    const char *dot = index(name, ':');
    if (dot != NULL) {
        name = dot + 1;
    }
    return name;
}

IFMapServerParser *IFMapServerParser::GetInstance(const string &module) {
    ModuleMap::iterator loc = module_map_.find(module);
    if (loc != module_map_.end()) {
        return loc->second;
    }
    IFMapServerParser *parser = new IFMapServerParser();
    module_map_.insert(make_pair(module, parser));
    return parser;
}

void IFMapServerParser::DeleteInstance(const string &module) {
    ModuleMap::iterator loc = module_map_.find(module);
    if (loc == module_map_.end()) {
        return;
    }

    delete loc->second;
    module_map_.erase(loc);
}

void IFMapServerParser::MetadataRegister(const string &metadata,
                                   MetadataParseFn parser) {
    pair<MetadataParseMap::iterator, bool> result =
            metadata_map_.insert(make_pair(metadata, parser));
    assert(result.second);
}

void IFMapServerParser::MetadataClear(const string &module) {
    metadata_map_.clear();
    DeleteInstance(module);
}

static string ParseIdentifier(const xml_node &node) {
    string id(node.attribute("name").value());
    return id;
}

void IFMapServerParser::SetOrigin(struct DBRequest *result) const {
    IFMapServerTable::RequestData *data =
            static_cast<IFMapServerTable::RequestData *>(result->data.get());
    data->origin.set_origin(IFMapOrigin::MAP_SERVER);
}

bool IFMapServerParser::ParseMetadata(const pugi::xml_node &node,
                                      struct DBRequest *result) const {
    IFMapServerTable::RequestData *data =
            static_cast<IFMapServerTable::RequestData *>(result->data.get());
    const char *name = NodeName(node);
    MetadataParseMap::const_iterator loc = metadata_map_.find(name);
    if (loc == metadata_map_.end()) {
        return false;
    }
    auto_ptr<AutogenProperty> pvalue;
    bool success = (loc->second)(node, &pvalue);
    if (!success) {
        return false;
    }
    if (data == NULL) {
        data = new IFMapServerTable::RequestData();
        result->data.reset(data);
    }
    data->metadata = name;
    data->content.reset(pvalue.release());
    return true;
}

// Expect the identifier name to be [contrail:]type:name
static void IdentifierNsTypeName(const string &id, string *id_type,
                                 string *id_name) {
    size_t ns = id.find("contrail:");
    size_t start = (ns == 0) ? sizeof("contrail:") - 1: 0;
    size_t loc = id.find(':', start);
    if (loc != string::npos) {
        *id_type = string(id, start, loc - start);
        *id_name = string(id, loc + 1, id.size() - (loc + 1));
    } else {
        *id_name = id;
    }
}

static DBRequest *IFMapServerRequestClone(const DBRequest *src) {
    DBRequest *request = new DBRequest();
    request->oper = src->oper;
    IFMapTable::RequestKey *src_key =
            static_cast<IFMapTable::RequestKey *>(src->key.get());
    if (src_key != NULL) {
        IFMapTable::RequestKey *dst_key =
                new IFMapTable::RequestKey();
        dst_key->id_type = src_key->id_type;
        dst_key->id_name = src_key->id_name;
        request->key.reset(dst_key);
    }

    IFMapServerTable::RequestData *src_data =
            static_cast<IFMapServerTable::RequestData *>(src->data.get());
    if (src_data) {
        IFMapServerTable::RequestData *dst_data =
                new IFMapServerTable::RequestData();
        dst_data->id_type = src_data->id_type;
        dst_data->id_name = src_data->id_name;
        request->data.reset(dst_data);
    }
    return request;
}

bool IFMapServerParser::ParseResultItem(
    const xml_node &parent, bool add_change, RequestList *list) const {
    auto_ptr<DBRequest> request(new DBRequest);
    request->oper = (add_change ? DBRequest::DB_ENTRY_ADD_CHANGE :
                     DBRequest::DB_ENTRY_DELETE);
    IFMapTable::RequestKey *key = NULL;
    IFMapServerTable::RequestData *data = NULL;
    int idcount = 0;
    bool has_meta = false;
    for (xml_node node = parent.first_child(); node;
         node = node.next_sibling()) {
        const char *name = NodeName(node);
        if (strcmp(name, "identity") == 0) {
            string id = ParseIdentifier(node);
            switch (idcount) {
            case 0:
                key = new IFMapTable::RequestKey();
                request->key.reset(key);
                IdentifierNsTypeName(id, &key->id_type, &key->id_name);
                break;
            case 1:
                data = new IFMapServerTable::RequestData();
                request->data.reset(data);
                IdentifierNsTypeName(id, &data->id_type, &data->id_name);
                break;
            default:
                return false;
            }
            idcount++;
            continue;
        }
        if (strcmp(name, "metadata") == 0) {
            if (has_meta) {
                return false;
            }
            has_meta = true;
            for (xml_node meta = node.first_child(); meta;
                 meta = meta.next_sibling()) {
                if (ParseMetadata(meta, request.get())) {
                    SetOrigin(request.get());
                    DBRequest *current = request.release();
                    if (meta.next_sibling()) {
                        request.reset(IFMapServerRequestClone(current));
                    }
                    list->push_back(current);
                }
            }
        }
    }

    if (idcount == 0 || !has_meta) {
        return false;
    }
    return true;
}

void IFMapServerParser::ParseResults(
    const xml_document &xdoc, RequestList *list) const {
    xml_node current = xdoc.first_child();
    while (current) {
        bool add_change;
        if (strcmp(NodeName(current), "updateResult") == 0 ||
            strcmp(NodeName(current), "searchResult") == 0 ||
            strcmp(NodeName(current), "deleteResult") == 0) {

            if (strcmp(NodeName(current), "deleteResult") == 0) {
                add_change = false;
            } else {
                add_change = true;
            }
            xml_node result = current;
            for (xml_node node = result.first_child(); node;
                node = node.next_sibling()) {
                ParseResultItem(node, add_change, list);
            }
            current = current.next_sibling();
        } else {
            current = current.first_child();
        }
    }
}

// Called in the context of the ifmap client thread.
bool IFMapServerParser::Receive(DB *db, const char *data, size_t length,
                                uint64_t sequence_number) {
    xml_document xdoc;
    pugi::xml_parse_result result = xdoc.load_buffer(data, length);
    if (!result) {
        IFMAP_WARN(IFMapXmlLoadError, "Unable to load XML document", length);
        return false;
    }

    IFMapServerParser::RequestList requests;
    ParseResults(xdoc, &requests);

    while (!requests.empty()) {
        auto_ptr<DBRequest> req(requests.front());
        requests.pop_front();

        IFMapTable::RequestKey *key =
                static_cast<IFMapTable::RequestKey *>(req->key.get());
        key->id_seq_num = sequence_number;

        IFMapTable *table = IFMapTable::FindTable(db, key->id_type);
        if (table != NULL) {
            table->Enqueue(req.get());
        } else {
            IFMAP_TRACE(IFMapTblNotFoundTrace, "Cant find table", key->id_type);
        }
    }
    return true;
}
