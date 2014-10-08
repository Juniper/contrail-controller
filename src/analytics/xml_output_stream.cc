/*
 * Copyright (c) 2014 Codilime.
 */

#include "analytics/xml_output_stream.h"
#include "analytics/stream_manager.h"
#include <boost/lexical_cast.hpp>

using analytics::XMLOutputStream;

// no other way to do it, I guess...
#define APPEND_CHILD(node_, header_, member_) \
    node_.append_child(#member_).append_child(pugi::node_pcdata).set_value( \
        boost::lexical_cast<std::string>(header_.member_).c_str())

bool XMLOutputStream::ProcessMessage(const SandeshStreamData &msg) {
    // Encode SandeshHeader
    pugi::xml_node header_node =
        msg.xml_doc.child("SandeshMessage").append_child("SandeshHeader");

    APPEND_CHILD(header_node, msg.header, Namespace); 
    APPEND_CHILD(header_node, msg.header, Timestamp); 
    APPEND_CHILD(header_node, msg.header, Source); 
    APPEND_CHILD(header_node, msg.header, Context); 
    APPEND_CHILD(header_node, msg.header, SequenceNum); 
    APPEND_CHILD(header_node, msg.header, VersionSig); 
    APPEND_CHILD(header_node, msg.header, Type); 
    APPEND_CHILD(header_node, msg.header, Hints); 
    APPEND_CHILD(header_node, msg.header, Level); 
    APPEND_CHILD(header_node, msg.header, Category); 
    APPEND_CHILD(header_node, msg.header, NodeType); 
    APPEND_CHILD(header_node, msg.header, InstanceId); 
    APPEND_CHILD(header_node, msg.header, IPAddress); 
    APPEND_CHILD(header_node, msg.header, Pid); 

    // print and send
    std::stringstream ostr;
    msg.xml_doc.print(ostr, "",
        pugi::format_raw |
        pugi::format_no_declaration |
        pugi::format_no_escapes);
    return SendRaw((const u_int8_t *)(ostr.str().c_str()), ostr.str().size());
}
