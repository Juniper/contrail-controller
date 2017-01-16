/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cass2json_adapter.h"

#include <assert.h>
#include <iostream>

#include <boost/assign/list_of.hpp>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "base/string_util.h"
#include "config_cassandra_client.h"
#include "config_json_parser.h"

using boost::assign::list_of;
using namespace rapidjson;
using namespace std;

const string ConfigCass2JsonAdapter::fq_name_prefix = "fq_name";
const string ConfigCass2JsonAdapter::prop_prefix = "prop:";
const string ConfigCass2JsonAdapter::list_prop_prefix = "propl:";
const string ConfigCass2JsonAdapter::map_prop_prefix = "propm:";
const string ConfigCass2JsonAdapter::child_prefix = "children:";
const string ConfigCass2JsonAdapter::ref_prefix = "ref:";
const string ConfigCass2JsonAdapter::meta_prefix = "META:";
const string ConfigCass2JsonAdapter::backref_prefix = "backref:";
const string ConfigCass2JsonAdapter::parent_prefix = "parent:";
const string ConfigCass2JsonAdapter::parent_type_prefix = "parent_type";
const string ConfigCass2JsonAdapter::comma_str = ",";

const set<string> ConfigCass2JsonAdapter::allowed_properties =
             list_of(prop_prefix)(map_prop_prefix)
                    (list_prop_prefix)(ref_prefix)(parent_prefix);

ConfigCass2JsonAdapter::ConfigCass2JsonAdapter(
       ConfigCassandraClient *cassandra_client, const string &obj_type,
       const CassColumnKVVec &cdvec) : cassandra_client_(cassandra_client),
    type_(""), prop_plen_(prop_prefix.size()),
    propl_plen_(list_prop_prefix.size()),
    propm_plen_(map_prop_prefix.size()),
    meta_plen_(meta_prefix.size()),
    backref_plen_(backref_prefix.size()),
    child_plen_(child_prefix.size()),
    ref_plen_(ref_prefix.size()),
    fq_name_plen_(fq_name_prefix.size()),
    parent_type_plen_(parent_type_prefix.size()),
    parent_plen_(parent_prefix.size()) {
    CreateJsonString(obj_type, cdvec);
}

string ConfigCass2JsonAdapter::GetAttrString(const Value &attr_value) {
    StringBuffer buffer;
    Writer<rapidjson::StringBuffer> writer(buffer);
    attr_value.Accept(writer);
    return buffer.GetString();
}

// Return true if the caller needs to append a comma. False otherwise.
bool ConfigCass2JsonAdapter::AddOneEntry(const string &obj_type,
                                         const CassColumnKVVec &cdvec, int i) {
    // If the key has 'prop:' at the start, remove it.
    if (cdvec.at(i).key.substr(0, prop_plen_) == prop_prefix) {
        if (cdvec.at(i).value != "null") {
            string prop_value = cdvec.at(i).value;
            if (cdvec.at(i).key.substr(prop_plen_) == "security_group_id") {
                prop_value = "\"" + prop_value + "\"";
            }
            doc_string_ += string("\"" + cdvec.at(i).key.substr(prop_plen_) +
                                  "\":" + prop_value);
        } else {
            return false;
        }
    } else if (cdvec.at(i).key.substr(0, propl_plen_) == list_prop_prefix) {
        size_t from_front_pos = cdvec.at(i).key.find(':');
        size_t from_back_pos = cdvec.at(i).key.rfind(':');
        string property_list = cdvec.at(i).key.substr(from_front_pos+1,
                                          (from_back_pos-from_front_pos-1));
        int index = 0;
        assert(stringToInteger(cdvec.at(i).key.substr(from_back_pos+1), index));
        ListPropertyMap::iterator it;
        if ((it = list_property_map_.find(property_list)) ==
            list_property_map_.end()) {
            std::pair<ListPropertyMap::iterator, bool> ret;
            ret = list_property_map_.insert(make_pair(property_list,
                                                      PropertyListDataMap()));
            assert(ret.second);
            it = ret.first;
        }
        it->second.insert(make_pair(index, cdvec.at(i).value));
        return false;
    } else if (cdvec.at(i).key.substr(0, propm_plen_) == map_prop_prefix) {
        size_t from_front_pos = cdvec.at(i).key.find(':');
        size_t from_back_pos = cdvec.at(i).key.rfind(':');
        string property_map = cdvec.at(i).key.substr(from_front_pos+1,
                                             (from_back_pos-from_front_pos-1));
        MapPropertyMap::iterator it;
        if ((it = map_property_map_.find(property_map)) ==
            map_property_map_.end()) {
            std::pair<MapPropertyMap::iterator, bool> ret;
            ret = map_property_map_.insert(make_pair(property_map,
                                                     PropertyMapDataList()));
            assert(ret.second);
            it = ret.first;
        }
        it->second.push_back(cdvec.at(i).value);
        return false;
    } else if (cdvec.at(i).key.substr(0, ref_plen_) == ref_prefix) {
        size_t from_front_pos = cdvec.at(i).key.find(':');
        size_t from_back_pos = cdvec.at(i).key.rfind(':');
        assert(from_front_pos != string::npos);
        assert(from_back_pos != string::npos);
        string ref_type = cdvec.at(i).key.substr(from_front_pos+1,
                                             (from_back_pos-from_front_pos-1));
        string ref_uuid = cdvec.at(i).key.substr(from_back_pos+1);

        string fq_name_ref = cassandra_client_->UUIDToFQName(ref_uuid);
        if (fq_name_ref == "ERROR")
            return false;
        RefTypeMap::iterator it;
        if ((it = ref_type_map_.find(ref_type)) == ref_type_map_.end()) {
            std::pair<RefTypeMap::iterator, bool> ret;
            ret = ref_type_map_.insert(make_pair(ref_type, RefDataList()));
            assert(ret.second);
            it = ret.first;
        }

        bool link_with_attr =
            cassandra_client_->mgr()->IsLinkWithAttr(obj_type, ref_type);
        if (link_with_attr) {
            Document ref_document;
            ref_document.Parse<0>(cdvec.at(i).value.c_str());
            Value& attr_value = ref_document["attr"];
            it->second.push_back(string("{\"to\":\"" + fq_name_ref +\
                        "\",\"uuid\":\"" + ref_uuid + "\",\"attr\":" +\
                        GetAttrString(attr_value) + "}"));
        } else {
            it->second.push_back(string("{\"to\":\"" + fq_name_ref +\
                                    "\", \"uuid\":\"" + ref_uuid + "\"}"));
        }
        return false;
    } else if (cdvec.at(i).key.substr(0, parent_type_plen_) ==
               parent_type_prefix) {
        // If the key has 'parent_type' at the start, ignore the column.
        return false;
    } else if (cdvec.at(i).key.substr(0, parent_plen_) == parent_prefix) {
        size_t pos = cdvec.at(i).key.rfind(':');
        assert(pos != string::npos);
        size_t type_pos = cdvec.at(i).key.find(':');
        assert(type_pos != string::npos);
        doc_string_ += string("\"parent_type\":\"" +\
          cdvec.at(i).key.substr(type_pos+1, pos-type_pos-1) + "\"");
    } else if (cdvec.at(i).key.substr(0, child_plen_) == child_prefix) {
        // If the key has 'children:' at the start, ignore the column.
        return false;
    } else if (cdvec.at(i).key.substr(0, backref_plen_) == backref_prefix) {
        // If the key has 'backref:' at the start, ignore the column.
        return false;
    } else if (cdvec.at(i).key.substr(0, meta_plen_) == meta_prefix) {
        // If the key has 'META:' at the start, ignore the column.
        return false;
    } else if (cdvec.at(i).key.substr(0, fq_name_plen_) == fq_name_prefix) {
        // Write the fq_name node
        doc_string_ +=
            string("\"" + cdvec.at(i).key + "\":" + cdvec.at(i).value);
    } else if (cdvec.at(i).key.compare("type") == 0) {
        // Prepend the 'type'. This is "our key", with value being the json
        // sub-document containing all other columns.
        assert(type_ != obj_type);
        type_ = cdvec.at(i).value;
        type_.erase(remove(type_.begin(), type_.end(), '\"' ), type_.end());
        doc_string_ = string("{" + cdvec.at(i).value + ":" + "{") +
                        doc_string_;
        return false;
    } else {
        cout << "Unknown tag:" << cdvec.at(i).key << endl;
        return false;
    }
    return true;
}

bool ConfigCass2JsonAdapter::CreateJsonString(const string &obj_type,
                                              const CassColumnKVVec &cdvec) {
    for (size_t i = 0; i < cdvec.size(); ++i) {
        if (AddOneEntry(obj_type, cdvec, i)) {
            doc_string_ += comma_str;
        }
    }

    assert(type_ != "");

    // Add the refs
    for (RefTypeMap::iterator it = ref_type_map_.begin();
         it != ref_type_map_.end(); it++) {
        doc_string_ += string("\"" + it->first + "_refs\":[");
        for (RefDataList::iterator rit = it->second.begin();
             rit != it->second.end(); rit++) {
            doc_string_ += *rit + comma_str;
        }
        // Remove the comma after the last entry.
        if (doc_string_[doc_string_.size() - 1] == ',') {
            doc_string_.erase(doc_string_.size() - 1);
        }
        doc_string_ += string("],");
    }

    // Add map properties
    for (MapPropertyMap::iterator it = map_property_map_.begin();
         it != map_property_map_.end(); it++) {
        string wrapper_field =
            cassandra_client_->mgr()->GetWrapperFieldName(type_, it->first);
        if (wrapper_field != "") wrapper_field = "\"" + wrapper_field + "\":";
        doc_string_ += string("\"" + it->first + "\":{" + wrapper_field + "[");
        for (PropertyMapDataList::iterator rit = it->second.begin();
             rit != it->second.end(); rit++) {
            doc_string_ += *rit + comma_str;
        }
        // Remove the comma after the last entry.
        if (doc_string_[doc_string_.size() - 1] == ',') {
            doc_string_.erase(doc_string_.size() - 1);
        }
        doc_string_ += string("] },");
    }

    // Add list properties
    for (ListPropertyMap::iterator it = list_property_map_.begin();
         it != list_property_map_.end(); it++) {
        string wrapper_field =
            cassandra_client_->mgr()->GetWrapperFieldName(type_, it->first);
        if (wrapper_field != "") wrapper_field = "\"" + wrapper_field + "\":";
        doc_string_ += string("\"" + it->first + "\":{" + wrapper_field + "[");
        for (PropertyListDataMap::iterator rit = it->second.begin();
             rit != it->second.end(); rit++) {
            doc_string_ += rit->second + comma_str;
        }
        // Remove the comma after the last entry.
        if (doc_string_[doc_string_.size() - 1] == ',') {
            doc_string_.erase(doc_string_.size() - 1);
        }
        doc_string_ += string("]},");
    }

    // Remove the comma after the last entry.
    if (doc_string_[doc_string_.size() - 1] == ',') {
        doc_string_.erase(doc_string_.size() - 1);
    }

    // Add one brace to close out the type's value and one to close out the
    // whole json document.
    doc_string_ += string("}}");

    return true;
}
