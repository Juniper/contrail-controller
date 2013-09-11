/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "viz_message.h"

RuleMsg::RuleMsg(const boost::shared_ptr<VizMsg> vmsgp) : hdr(vmsgp->hdr),
    messagetype(vmsgp->messagetype) {
    pugi::xml_parse_result result = doc_.load(vmsgp->xmlmessage.c_str());
    if (!result) {
        LOG(ERROR, __func__ << ": ERROR parsing XML: " << result.description()
            << " Message: " << vmsgp->xmlmessage);
    }
}

RuleMsg::~RuleMsg() {
}

int RuleMsg::field_value_recur(const std::string& field_id, std::string& type, std::string& value, pugi::xml_node doc) const {
    size_t dotpos;
    if ((dotpos = field_id.find_first_of('.')) != std::string::npos) {
        std::string parent = field_id.substr(0, dotpos);
        std::string children = field_id.substr(dotpos+1);

        RuleMsgPredicate p1(parent);

        pugi::xml_node node = doc.find_node(p1);
        if (node.type() == pugi::node_null) {
            return -1;
        }

        const char *ret;
        ret = node.attribute("type").value();
        if (strncmp("struct", ret, 6))
            return -1;

        return (field_value_recur(children, type, value, node));
    }
    
    RuleMsgPredicate p1(field_id);
    pugi::xml_node node = doc.find_node(p1);
    if (node.type() == pugi::node_null) {
        return -1;
    }
    value = node.child_value();;
    type = node.attribute("type").value();

    return 0;
}

int RuleMsg::field_value(const std::string& field_id, std::string& type, std::string& value) const {
    return field_value_recur(field_id, type, value, doc_);
}
