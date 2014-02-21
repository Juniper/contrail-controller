/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <sandesh/sandesh_message_builder.h>
#include "viz_message.h"

RuleMsg::RuleMsg(const VizMsg* vmsgp) : 
    hdr(vmsgp->msg->GetHeader()),
    messagetype(vmsgp->msg->GetMessageType()),
    message_node_(static_cast<const SandeshXMLMessage *>(
        vmsgp->msg)->GetMessageNode()) {
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
    return field_value_recur(field_id, type, value, message_node_);
}
