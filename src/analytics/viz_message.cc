/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <base/util.h>
#include <sandesh/sandesh_message_builder.h>
#include "collector_uve_types.h"
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

// VizMsgStatistics
VizMsgStats operator+(const VizMsgStats &a, const VizMsgStats &b) {
    VizMsgStats sum;
    sum.messages = a.messages + b.messages;
    sum.bytes = a.bytes + b.bytes;
    return sum;
}
 
VizMsgStats operator-(const VizMsgStats &a, const VizMsgStats &b) {
    VizMsgStats diff;
    diff.messages = a.messages - b.messages;
    diff.bytes = a.bytes - b.bytes;
    return diff;
}

void VizMsgStats::Update(const VizMsg *vmsg) {
    const SandeshHeader &header(vmsg->msg->GetHeader());
    messages++;
    bytes += vmsg->msg->GetSize();
    last_msg_timestamp = header.get_Timestamp();
}

template <typename T, typename K>
void GetStats(T &stats, const VizMsgStats *vmstats, K &key) {
    stats.set_messages(vmstats->messages);
    stats.set_bytes(vmstats->bytes);
    stats.set_last_msg_timestamp(vmstats->last_msg_timestamp);
}

template <>
void GetStats<>(SandeshMessageInfo &sminfo,
    const VizMsgStats *vmstats, VizMsgStatistics::TypeLevelKey &key) {
    sminfo.set_type(key.first);
    sminfo.set_level(key.second); 
    sminfo.set_messages(vmstats->messages);
    sminfo.set_bytes(vmstats->bytes);
}

template <typename K, typename T>
void VizMsgStats::Get(K &key, T &stats) const {
    GetStats<T>(stats, this, key);
}

template <typename Table, typename Key>
static void UpdateStats(Table &t, Key &k, const VizMsg *vmsg) {
    typename Table::iterator it = t.find(k);
    if (it == t.end()) {
        it = (t.insert(k, new VizMsgStats)).first;
    }
    VizMsgStats *vmstats = it->second;
    vmstats->Update(vmsg);
}
    
void VizMsgStatistics::Update(const VizMsg *vmsg) {
    // TypeMap
    std::string messagetype(vmsg->msg->GetMessageType());
    UpdateStats<TypeMap, std::string>(type_map, messagetype, vmsg);
    // LogLevelMap for only system log and syslog
    const SandeshHeader &header(vmsg->msg->GetHeader());
    SandeshLevel::type level(static_cast<SandeshLevel::type>(
        header.get_Level()));
    std::string level_str(Sandesh::LevelToString(level));
    const SandeshType::type &stype(header.get_Type());
    if (stype == SandeshType::SYSTEM ||
        stype == SandeshType::SYSLOG) {
        UpdateStats<LevelMap, std::string>(level_map, level_str,
            vmsg);
    }
    // TypeLevelMap
    TypeLevelKey tlkey(messagetype, level_str);
    UpdateStats<TypeLevelMap, TypeLevelKey>(type_level_map, tlkey, vmsg);
}

// TypeMap
void VizMsgStatistics::Get(std::vector<SandeshStats> &ssv) const {
    for (TypeMap::const_iterator it = type_map.begin();
         it != type_map.end(); it++) {
        SandeshStats sstats;
        sstats.set_message_type(it->first);
        const VizMsgStats *vmstats(it->second);
        vmstats->Get(it->first, sstats);
        ssv.push_back(sstats);
    }
}

// LogLevelMap
void VizMsgStatistics::Get(std::vector<SandeshLogLevelStats> &lsv) const {
    for (LevelMap::const_iterator it = level_map.begin();
         it != level_map.end(); it++) {
        SandeshLogLevelStats lstats;
        lstats.set_level(it->first);
        const VizMsgStats *vmstats(it->second);
        vmstats->Get(it->first, lstats);
        lsv.push_back(lstats);
    }
}

// TypeLevelMap
void VizMsgStatistics::Get(std::vector<SandeshMessageInfo> &smv) {
    // Send diffs 
   GetDiffStats<TypeLevelMap, TypeLevelKey, VizMsgStats, SandeshMessageInfo>(
       type_level_map, otype_level_map, smv);
}
