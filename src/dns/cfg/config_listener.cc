/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "cfg/config_listener.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include "bind/bind_util.h"
#include "cfg/dns_config.h"
#include "db/db.h"
#include "db/db_table_partition.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace boost::assign;
using namespace std;

// The DependencyTracker adds elements onto the change list that must be
// reevaluated when links are added or deleted from the graph.
class ConfigListener::DependencyTracker {
public:
    struct EdgeDescriptor {
        EdgeDescriptor(const string &meta, const string &type,
                        const string &name)
            : metadata(meta), id_type(type), id_name(name) {
        }
        string metadata;
        string id_type;
        string id_name;
    };
    typedef list<EdgeDescriptor> EdgeDescriptorList;
    typedef list<pair<string, string> > NodeList;

    // identifier type -> (incoming, outlist)
    typedef set<string> PropagateList;
    typedef map<string, PropagateList> ReactionMap;
    typedef map<string, ReactionMap> NodeEventPolicy;

    DependencyTracker(ConfigListener *listener) : listener_(listener) {
        Initialize();
    }

    void Initialize() {
        ReactionMap ipam_react =
            map_list_of<string, PropagateList>
                ("self", list_of("self"))
                ("virtual-network-network-ipam", list_of("self"));
        policy_.insert(make_pair("network-ipam", ipam_react));
#if 0
        ReactionMap vnni_react =
            map_list_of<string, PropagateList>
                ("self", list_of("self"))
                ("network-ipam", list_of("self"))
                ("virtual-network-network-ipam", list_of("self"));
        policy_.insert(make_pair("virtual-network-network-ipam", vnni_react));
#endif
        ReactionMap virt_dns_react =
            map_list_of<string, PropagateList>
                ("self", list_of("self"));
        policy_.insert(make_pair("virtual-DNS", virt_dns_react));
        ReactionMap virt_dns_rec_react =
            map_list_of<string, PropagateList>
                ("self", list_of("self"))
                ("virtual-DNS-virtual-DNS-record", list_of("self"));
        policy_.insert(make_pair("virtual-DNS-record", virt_dns_rec_react));
    }

    void NodeEvent(IFMapNode *node) {
        ostringstream identifier;
        identifier << node->table()->Typename() << ':' << node->name();
        vertex_list_.insert(identifier.str());
        if (IsInterestingEvent(node, "self")) {
            node_list_.push_back(
                make_pair(node->table()->Typename(), node->name()));
        }
    }

    bool LinkEvent(const string metadata,
                   IFMapNode *left, IFMapNode *right) {
        bool interest = false;
        if ((left != NULL) && IsInterestingEvent(left, metadata)) {
            const char *type_left = left->table()->Typename();
            edge_list_.push_back(
                EdgeDescriptor(metadata, type_left, left->name()));
            interest = true;
        }
        if ((right != NULL) && IsInterestingEvent(right, metadata)) {
            const char *type_right = right->table()->Typename();
            edge_list_.push_back(
                EdgeDescriptor(metadata, type_right, right->name()));
            interest = true;
        }
        return interest;
    }

    void PropagateChanges(ChangeList *change_list) {
        InEdgeSet in_edges;

        for (NodeList::iterator iter = node_list_.begin();
             iter != node_list_.end(); ++iter) {
            IFMapTable *table = IFMapTable::FindTable(listener_->database(),
                                                      iter->first);
            if (table == NULL) {
                continue;
            }
            IFMapNode *node = table->FindNode(iter->second);
            if ((node == NULL) || node->IsDeleted()) {
                continue;
            }
            PropagateNode(node, &in_edges, change_list);
        }

        for (EdgeDescriptorList::iterator iter = edge_list_.begin();
             iter != edge_list_.end(); ++iter) {
            const EdgeDescriptor &edge = *iter;
            IFMapTable *table = IFMapTable::FindTable(listener_->database(),
                                                      edge.id_type);
            if (table == NULL) {
                continue;
            }
            IFMapNode *node = table->FindNode(edge.id_name);
            if ((node == NULL) || node->IsDeleted()) {
                continue;
            }
            PropagateEdge(node, edge.metadata, &in_edges, change_list);
        }
    }

    void Clear() {
        vertex_list_.clear();
        node_list_.clear();
        edge_list_.clear();
    }

private:
    typedef set<pair<IFMapNode *, string> > InEdgeSet;

    const PropagateList *GetPropagateList(const string &type,
                                          const string &metadata) const {
        NodeEventPolicy::const_iterator loc = policy_.find(type);
        if (loc == policy_.end()) {
            return NULL;
        }
        ReactionMap::const_iterator react = loc->second.find(metadata);
        if (react == loc->second.end()) {
            return NULL;
        }
        return &react->second;
    }
    
    bool IsInterestingEvent(const IFMapNode *node,
                            const string &metadata) const {
        if (node->IsDeleted()) {
            return false;
        }
        return GetPropagateList(node->table()->Typename(), metadata) != NULL;
    }

    void PropagateNode(IFMapNode *node, InEdgeSet *in_edges,
                       ChangeList *change_list) {
        // iterate through the edges of node.
        for (DBGraphVertex::edge_iterator iter =
             node->edge_list_begin(listener_->graph());
             iter != node->edge_list_end(listener_->graph()); ++iter) {
            IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
            IFMapNode *target = static_cast<IFMapNode *>(iter.target());
            const PropagateList *plist = GetPropagateList(
                    target->table()->Typename(), link->metadata());
            if (plist == NULL) {
                continue;
            }
            PropagateEdge(target, link->metadata(), in_edges, change_list);            
        }
    }

    void PropagateEdge(IFMapNode *node, const string &metadata,
                       InEdgeSet *in_edges, ChangeList *change_list) {
        const PropagateList *plist = GetPropagateList(node->table()->Typename(),
                                                      metadata);
        if (plist == NULL) {
            return;
        }
        assert(!node->IsDeleted());
        in_edges->insert(make_pair(node, metadata));

        PropagateList::const_iterator self = plist->find("self");
        if (self != plist->end()) {
            AddChangeEvent(change_list, node);
        }

        // iterate through the edges of node.
        for (DBGraphVertex::edge_iterator iter =
             node->edge_list_begin(listener_->graph());
             iter != node->edge_list_end(listener_->graph()); ++iter) {
            IFMapLink *link = static_cast<IFMapLink *>(iter.operator->());
            PropagateList::const_iterator p_edge =
                plist->find(link->metadata());
            if (p_edge == plist->end()) {
                continue;
            }
            IFMapNode *target = static_cast<IFMapNode *>(iter.target());
            if (in_edges->count(make_pair(target, link->metadata())) > 0) {
                continue;
            }
            PropagateEdge(target, link->metadata(), in_edges, change_list);
        }
    }

    void AddChangeEvent(ChangeList *change_list, IFMapNode *node) {
        ostringstream identifier;
        identifier << node->table()->Typename() << ':' << node->name();
        if (vertex_list_.count(identifier.str()) > 0) {
            return;
        }
        listener_->ChangeListAdd(change_list, node);
        vertex_list_.insert(identifier.str());
    }

    ConfigListener *listener_;
    NodeEventPolicy policy_;
    set<string> vertex_list_;
    EdgeDescriptorList edge_list_;
    NodeList node_list_;
};

ConfigListener::ConfigListener(DnsConfigManager *manager)
    : manager_(manager), tracker_(new DependencyTracker(this)) {
}

ConfigListener::~ConfigListener() {
}

void ConfigListener::Initialize(DB *database, int ntypes, 
                                const char *config_types[]) {
    DBTable *link_table = static_cast<DBTable *>(
        database->FindTable("__ifmap_metadata__.0"));
    assert(link_table != NULL);
    DBTable::ListenerId id = link_table->Register(
        boost::bind(&ConfigListener::LinkObserver, this, _1, _2));
    table_map_.insert(make_pair(link_table->name(), id));

    for (int i = 0; i < ntypes; i++) {
        const char *type_name = config_types[i];
        IFMapTable *table = IFMapTable::FindTable(database, type_name);
        assert(table);
        DBTable::ListenerId id = table->Register(
                boost::bind(&ConfigListener::NodeObserver, this, _1, _2));
        table_map_.insert(make_pair(table->name(), id));
    }
}

void ConfigListener::Terminate(DB *database) {
    for (TableMap::iterator iter = table_map_.begin(); iter != table_map_.end();
         ++iter) {
        IFMapTable *table = static_cast<IFMapTable *>(
                database->FindTable(iter->first));
        assert(table);
        table->Unregister(iter->second);
    }
    table_map_.clear();
}

DB *ConfigListener::database() {
    return manager_->database();
}

DBGraph *ConfigListener::graph() {
    return manager_->graph();
}

void ConfigListener::GetChangeList(ChangeList *change_list) {
    tracker_->PropagateChanges(&change_list_);
    tracker_->Clear();
    change_list->swap(change_list_);
}

void ConfigListener::ChangeListAdd(ChangeList *change_list,
                                   IFMapNode *node) const {
    IFMapTable *table = node->table();
    TableMap::const_iterator tid = table_map_.find(table->name());
    if (tid == table_map_.end()) {
        return;
    }

    ConfigDelta delta;
    delta.id_type = table->Typename();
    delta.id_name = node->name();
    if (!node->IsDeleted()) {

        DBState *current = node->GetState(table, tid->second);
        if (current == NULL) {
            delta.node = IFMapNodeRef(new IFMapNodeProxy(node, tid->second));
        }
        delta.obj = IFMapObjectRef(node->GetObject());
    }
    change_list->push_back(delta);
}

void ConfigListener::NodeObserver(DBTablePartBase *root,
                                  DBEntryBase *db_entry) {
    IFMapNode *node = static_cast<IFMapNode *>(db_entry);
    // Ignore deleted nodes for which the configuration code doesn't hold state.
    if (node->IsDeleted()) {
        IFMapTable *table = node->table();
        TableMap::const_iterator tid = table_map_.find(table->name());
        assert(tid != table_map_.end());
        if (node->GetState(table, tid->second) == NULL) {
            return;
        }
    }
    ChangeListAdd(&change_list_, node);
    tracker_->NodeEvent(node);
    manager_->OnChange();
}

void ConfigListener::LinkObserver(DBTablePartBase *root,
                                  DBEntryBase *db_entry) {
    IFMapLink *link = static_cast<IFMapLink *>(db_entry);
    IFMapNode *left = link->LeftNode(database());
    IFMapNode *right = link->RightNode(database());
    if (tracker_->LinkEvent(link->metadata(), left, right)) {
        manager_->OnChange();
    }
}
