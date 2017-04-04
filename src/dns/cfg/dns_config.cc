/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include "base/logging.h"
#include "base/task.h"
#include "base/util.h"
#include "bind/bind_util.h"
#include "cfg/dns_config.h"
#include "cfg/config_listener.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_table.h"
#include "cmn/dns.h"

using namespace std;

static const char *config_types[] = {
    "virtual-DNS",
    "virtual-DNS-record",
    "network-ipam",
    "virtual-network-network-ipam",
    "global-qos-config",
};

const std::string DnsConfigManager::EventString[] = {
    "None",
    "Add",
    "Change",
    "Delete"
};

SandeshTraceBufferPtr DnsConfigTraceBuf(SandeshTraceBufferCreate("Config", 2000));
int DnsConfigManager::config_task_id_ = -1;
const int DnsConfigManager::kConfigTaskInstanceId;

ConfigDelta::ConfigDelta() {
}

ConfigDelta::ConfigDelta(const ConfigDelta &rhs)
    : id_type(rhs.id_type), id_name(rhs.id_name),
      node(rhs.node), obj(rhs.obj) {
}

ConfigDelta::~ConfigDelta() {
}

DnsConfigManager::DnsConfigManager()
        : db_(NULL), db_graph_(NULL),
          trigger_(boost::bind(&DnsConfigManager::ConfigHandler, this),
                   TaskScheduler::GetInstance()->GetTaskId("dns::Config"), 0),
          listener_(new ConfigListener(this)) {
    IdentifierMapInit();

    if (config_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        config_task_id_ = scheduler->GetTaskId("dns::Config");
    }
}

DnsConfigManager::~DnsConfigManager() {
}

void DnsConfigManager::Initialize(DB *db, DBGraph *db_graph) {
    db_ = db;
    db_graph_ = db_graph;
    int ntypes = sizeof(config_types) / sizeof(const char *);
    listener_->Initialize(db, ntypes, config_types);
}

void DnsConfigManager::OnChange() {
    trigger_.Set();
}

void DnsConfigManager::IdentifierMapInit() {
    id_map_.insert(make_pair("network-ipam",
            boost::bind(&DnsConfigManager::ProcessNetworkIpam, this, _1)));
    id_map_.insert(make_pair("virtual-network-network-ipam",
            boost::bind(&DnsConfigManager::ProcessVNNI, this, _1)));
    id_map_.insert(make_pair("virtual-DNS",
            boost::bind(&DnsConfigManager::ProcessVirtualDNS, this, _1)));
    id_map_.insert(make_pair("virtual-DNS-record",
            boost::bind(&DnsConfigManager::ProcessVirtualDNSRecord, this, _1)));
    id_map_.insert(make_pair("global-qos-config",
            boost::bind(&DnsConfigManager::ProcessGlobalQosConfig, this, _1)));
}

void DnsConfigManager::ProcessNode(const ConfigDelta &delta,
                                   DnsConfigData &config_data,
                                   Observer observer) {
    IFMapNodeProxy *proxy = config_data.Find(delta.id_name);
    if (!proxy) {
        proxy = delta.node.get();
        if (proxy == NULL)
            return;
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted())
            return;
        config_data.Add(delta.id_name, delta.node);
        if (observer) {
            (observer)(proxy, delta.id_name, DnsConfigManager::CFG_ADD);
        }
        DNS_TRACE(DnsConfigTrace, "Add : Type = " + delta.id_type +
                  " Name = " + delta.id_name);
    } else {
        IFMapNode *node = proxy->node();
        if (node->IsDeleted() || !node->HasAdjacencies(db_graph_)) {
            if (observer) {
                (observer)(proxy, delta.id_name,
                           DnsConfigManager::CFG_DELETE);
            }
            config_data.Del(delta.id_name);
            DNS_TRACE(DnsConfigTrace, "Delete : Type = " + delta.id_type +
                      " Name = " + delta.id_name);
        } else {
            if (observer) {
                (observer)(proxy, delta.id_name,
                           DnsConfigManager::CFG_CHANGE);
            }
            DNS_TRACE(DnsConfigTrace, "Change : Type = " + delta.id_type +
                      " Name = " + delta.id_name);
        }
    }
}

void DnsConfigManager::ProcessVirtualDNS(const ConfigDelta &delta) {
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsConfigTrace, "Error - Received Virtual DNS without a name");
        return;
    }
    ProcessNode(delta, virt_dns_config_, obs_.virtual_dns);
}

void DnsConfigManager::ProcessVirtualDNSRecord(const ConfigDelta &delta) {
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsConfigTrace, "Error - Received Virtual DNS Record without a name");
        return;
    }
    ProcessNode(delta, virt_dns_rec_config_, obs_.virtual_dns_record);
}

void DnsConfigManager::ProcessVNNI(const ConfigDelta &delta) {
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsConfigTrace, "Error - Received VNNI without a name");
        return;
    }
    ProcessNode(delta, vnni_config_, obs_.vnni);
}

void DnsConfigManager::ProcessNetworkIpam(const ConfigDelta &delta) {
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsConfigTrace, "Error - Received Ipam without a name");
        return;
    }
    ProcessNode(delta, ipam_config_, obs_.ipam);
}

void DnsConfigManager::ProcessGlobalQosConfig(const ConfigDelta &delta) {
    if (!delta.id_name.size()) {
        DNS_TRACE(DnsConfigTrace, "Error - Received GlobalQosConfig without a"
                  " name");
        return;
    }
    ProcessNode(delta, global_qos_config_, obs_.global_qos);
}

IFMapNode *DnsConfigManager::FindTarget(IFMapNode *node, 
                                        std::string link_name) {
    for (DBGraphVertex::edge_iterator iter = node->edge_list_begin(graph());
         iter != node->edge_list_end(graph()); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        if (link->metadata() == link_name)
            return static_cast<IFMapNode *>(iter.target());
    }
    return NULL;
}

IFMapNode *DnsConfigManager::FindTarget(IFMapNode *node,
                                        std::string link_name, 
                                        std::string node_type) {
    for (DBGraphVertex::edge_iterator iter = node->edge_list_begin(graph());
         iter != node->edge_list_end(graph()); ++iter) {
        IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
        if (link->metadata() == link_name) {
            IFMapNode *targ_node = static_cast<IFMapNode *>(iter.target());
            if (targ_node->table()->Typename() == node_type)
                return targ_node;
        }       
    }
    return NULL;
}

void DnsConfigManager::ProcessChanges(const ChangeList &change_list) {
    for (ChangeList::const_iterator iter = change_list.begin();
         iter != change_list.end(); ++iter) {
        IdentifierMap::iterator loc = id_map_.find(iter->id_type);
        if (loc != id_map_.end()) {
            (loc->second)(*iter);
        }
    }
}

bool DnsConfigManager::ConfigHandler() {
    ConfigListener::ChangeList change_list;
    listener_->GetChangeList(&change_list);
    ProcessChanges(change_list);
    return true;
}

void DnsConfigManager::Terminate() {
    listener_->Terminate(db_);
}

