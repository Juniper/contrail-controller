/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cass2json_adapter.h"

#include <iostream>

using namespace std;

const string ConfigCass2JsonAdapter::prop_prefix = "prop:";
const string ConfigCass2JsonAdapter::meta_prefix = "META:";
const string ConfigCass2JsonAdapter::comma_str = ",";

ConfigCass2JsonAdapter::ConfigCass2JsonAdapter(const CassColumnKVVec &cdvec)
        : prop_plen_(prop_prefix.size()),
          meta_plen_(meta_prefix.size()) {
    CreateJsonString(cdvec);
}

// Return true if the caller needs to append a comma. False otherwise.
bool ConfigCass2JsonAdapter::AddOneEntry(const CassColumnKVVec &cdvec, int i) {
    // If the key has 'prop:' at the start, remove it.
    if (cdvec.at(i).key.substr(0, prop_plen_) == prop_prefix) {
        doc_string_ += string(
            "\"" + cdvec.at(i).key.substr(prop_plen_) +
            "\"" + ": " + cdvec.at(i).value);
    } else if (cdvec.at(i).key.substr(0, meta_plen_) == meta_prefix) {
        // If the key has 'META:' at the start, ignore the column.
        return false;
    } else if (cdvec.at(i).key.compare("type") == 0) {
        // Prepend the 'type'. This is "our key", with value being the json
        // sub-document containing all other columns.
        doc_string_ = string("{\n" + cdvec.at(i).value + ":" + "{\n") +
                        doc_string_;
        return false;
    } else {
        doc_string_ += string("\"" + cdvec.at(i).key + "\"" + ": " +
                                cdvec.at(i).value);
    }
    return true;
}

bool ConfigCass2JsonAdapter::CreateJsonString(const CassColumnKVVec &cdvec) {
    for (size_t i = 0; i < cdvec.size(); ++i) {
        if (AddOneEntry(cdvec, i)) {
            doc_string_ += comma_str;
        }
    }

    // Remove the comma after the last entry.
    if (doc_string_[doc_string_.size() - 1] == ',') {
        doc_string_.erase(doc_string_.size() - 1);
    }

    // Add one brace to close out the type's value and one to close out the
    // whole json document.
    doc_string_ += string("\n}\n}");

    return true;
}

