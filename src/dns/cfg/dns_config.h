/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DNS_CONFIG_H__
#define __DNS_CONFIG_H__

#include <list>
#include <map>
#include <set>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/ptr_container/ptr_map.hpp>

#include "base/util.h"
#include "base/queue_task.h"
#include "base/task_trigger.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_node_proxy.h"
#include "vnc_cfg_types.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "cmn/dns_types.h"

class ConfigListener;
class DB;
class DBGraph;
class IFMapNodeProxy;

extern SandeshTraceBufferPtr DnsConfigTraceBuf;

#define DNS_TRACE(Obj, ...)                                                   \
do {                                                                          \
    Obj::TraceMsg(DnsConfigTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);      \
} while (false)  

typedef boost::shared_ptr<IFMapNodeProxy> IFMapNodeProxyRef;

struct ConfigDelta {
    ConfigDelta();
    ConfigDelta(const ConfigDelta &rhs);
    ~ConfigDelta();
    std::string id_type;
    std::string id_name;
    IFMapNodeProxyRef node;
    IFMapObjectRef obj;
};

struct DnsConfigData {
    typedef std::map<std::string, IFMapNodeProxyRef> DataMap;
    typedef std::pair<std::string, IFMapNodeProxyRef> DataPair;

    DataMap data_;

    void Add(std::string name, IFMapNodeProxyRef node) {
        data_.insert(DataPair(name, node));
    }

    void Del(std::string name) {
        data_.erase(name);
    }

    IFMapNodeProxy *Find(std::string name) {
        DataMap::iterator iter = data_.find(name);
        if (iter != data_.end())
            return iter->second.get();
        return NULL;
    }
};

class DnsConfigManager {
public:
    static const int kConfigTaskInstanceId = 0;
    enum EventType {
        CFG_NONE,
        CFG_ADD,
        CFG_CHANGE,
        CFG_DELETE
    };
    static const std::string EventString[];

    typedef boost::function<void(IFMapNodeProxy *,
                                 const std::string&, EventType)> Observer;
    struct Observers {
        Observer virtual_dns;
        Observer virtual_dns_record;
        Observer ipam;
        Observer vnni;
        Observer global_qos;
    };

    DnsConfigManager();
    virtual ~DnsConfigManager();
    void Initialize(DB *db, DBGraph *db_graph);
    void RegisterObservers(const Observers &obs) { obs_ = obs; }

    DB *database() { return db_; }
    DBGraph *graph() { return db_graph_; }

    void OnChange();

    const std::string &localname() const { return localname_; }

    const std::string &ToEventString(EventType ev) { return EventString[ev]; }
    void Terminate();

    IFMapNode *FindTarget(IFMapNode *node, std::string link_name);
    IFMapNode *FindTarget(IFMapNode *node, std::string link_name, 
                          std::string node_type);
private:
    typedef std::vector<ConfigDelta> ChangeList;
    typedef std::map<std::string,
        boost::function<void(const ConfigDelta &)> >IdentifierMap;

    void IdentifierMapInit();
    void ProcessChanges(const ChangeList &change_list);
    void ProcessNetworkIpam(const ConfigDelta &delta);
    void ProcessVNNI(const ConfigDelta &delta);
    void ProcessVirtualDNS(const ConfigDelta &delta);
    void ProcessVirtualDNSRecord(const ConfigDelta &delta);
    void ProcessGlobalQosConfig(const ConfigDelta &delta);
    void ProcessNode(const ConfigDelta &delta, DnsConfigData &config_data,
                     Observer observer);

    bool ConfigHandler();
    static int config_task_id_;

    DB *db_;
    DBGraph *db_graph_;
    std::string localname_;
    IdentifierMap id_map_;
    DnsConfigData ipam_config_;
    DnsConfigData vnni_config_;
    DnsConfigData virt_dns_config_;
    DnsConfigData virt_dns_rec_config_;
    DnsConfigData global_qos_config_;
    Observers obs_;
    TaskTrigger trigger_;
    boost::scoped_ptr<ConfigListener> listener_;
    DISALLOW_COPY_AND_ASSIGN(DnsConfigManager);
};


#endif // __DNS_CONFIG_H__
