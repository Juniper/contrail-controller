/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_agent_table.h"

#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include "base/logging.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_table_partition.h"
#include "ifmap/ifmap_agent_parser.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_link.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_types.h"
#include "ifmap/ifmap_agent_types.h"

using namespace std;

SandeshTraceBufferPtr
IFMapAgentTraceBuf(SandeshTraceBufferCreate("IFMapAgentTrace", 1000));

IFMapAgentTable::IFMapAgentTable(DB *db, const string &name, DBGraph *graph)
        : IFMapTable(db, name, graph), pre_filter_(NULL) {
}

auto_ptr<DBEntry> IFMapAgentTable::AllocEntry(const DBRequestKey *key) const {
    auto_ptr<DBEntry> entry(
        new IFMapNode(const_cast<IFMapAgentTable *>(this)));
    entry->SetKey(key);
    return entry;
}

IFMapNode* IFMapAgentTable::TableEntryLookup(DB *db, RequestKey *key) {

    IFMapTable *table = FindTable(db, key->id_type);
    if (!table) {
        return NULL;
    }

    auto_ptr<DBEntry> entry(new IFMapNode(table));
    entry->SetKey(key);
    IFMapNode *node = static_cast<IFMapNode *>(table->Find(entry.get()));
    return node;
}


IFMapAgentTable* IFMapAgentTable::TableFind(const string &node_name) {
    string name = node_name;
    boost::replace_all(name, "-", "_");
    name = "__ifmap__." + name + ".0";
    IFMapAgentTable *table =
            static_cast<IFMapAgentTable *>(database()->FindTable(name));
    return table;
}

IFMapNode *IFMapAgentTable::EntryLookup(RequestKey *request) {
    auto_ptr<DBEntry> key(AllocEntry(request));
    IFMapNode *node = static_cast<IFMapNode *>(Find(key.get()));
    return node;
}

IFMapNode *IFMapAgentTable::EntryLocate(IFMapNode *node, RequestKey *req) {

    IFMapObject *obj;

    if (node != NULL) {
        /* If delete marked, clear it now */
        if (node->IsDeleted()) {
            node->ClearDelete();
            graph()->AddNode(node);
        }

        obj = node->GetObject();
        assert(obj);
        //We dont accept lesser sequence number updates
        assert(obj->sequence_number() <= req->id_seq_num);

        node->Remove(obj);

    } else {
        auto_ptr<DBEntry> key(AllocEntry(req));
        node = const_cast<IFMapNode *>(
            static_cast<const IFMapNode *>(key.release()));
        DBTablePartition *partition =
            static_cast<DBTablePartition *>(GetTablePartition(0));
        partition->Add(node);
        graph()->AddNode(node);
    }

    return node;
}

// A node is deleted. Move all links for the node to defer-list
void IFMapAgentTable::HandlePendingLinks(IFMapNode *node) {

    IFMapNode *right;
    DBGraphEdge *edge;

    IFMapAgentLinkTable *ltable = static_cast<IFMapAgentLinkTable *>
        (database()->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(ltable != NULL);

    DBGraphVertex::edge_iterator iter;
    bool origin_exists;
    uint64_t seq;
    for (iter = node->edge_list_begin(graph());
         iter != node->edge_list_end(graph());) {
        edge = iter.operator->();
        IFMapLink *l = static_cast<IFMapLink *>(edge);
        right = static_cast<IFMapNode *>(iter.target());
        iter++;
        seq = l->sequence_number(IFMapOrigin::UNKNOWN, &origin_exists);
        assert(origin_exists);
        // Create both the request keys
        auto_ptr <IFMapAgentLinkTable::RequestKey> req_key (new IFMapAgentLinkTable::RequestKey);
        req_key->left_key.id_name = node->name();
        req_key->left_key.id_type = node->table()->Typename();
        req_key->left_key.id_seq_num = seq;

        req_key->right_key.id_name = right->name();
        req_key->right_key.id_type = right->table()->Typename();
        req_key->right_key.id_seq_num = seq;
        req_key->metadata = l->metadata();

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key = req_key;

        //Add it to defer list
        ltable->LinkDefAdd(&req);

        ltable->DelLink(node, right, edge);
    }
}

void IFMapAgentTable::DeleteNode(IFMapNode *node) {


    if ((node->HasAdjacencies(graph()) == true)) {
        HandlePendingLinks(node);
    }

    //Now there should not be any more adjacencies
    assert((node->HasAdjacencies(graph()) == false));

    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    graph()->RemoveNode(node);
    partition->Delete(node);
}

void IFMapAgentTable::NotifyNode(IFMapNode *node) {
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(GetTablePartition(0));
    partition->Change(node);
}

// Process link-defer list based for the request.
// If request is add, create left->right and right-left defer nodes
// If request is delete, remove left->right and right->left defer nodes
// The sequence number is valid only in the DeferrendNode entry
void IFMapAgentLinkTable::LinkDefAdd(DBRequest *request) {
    RequestKey *key = static_cast<RequestKey *>(request->key.get());

    std::list<DeferredNode>::iterator it;

    std::list<DeferredNode> *left = NULL;
    LinkDefMap::iterator left_it = link_def_map_.find(key->left_key);
    if (link_def_map_.end() != left_it)
        left = left_it->second;

    std::list<DeferredNode> *right = NULL;
    LinkDefMap::iterator right_it = link_def_map_.find(key->right_key);
    if (link_def_map_.end() != right_it)
        right = right_it->second;

    if (request->oper == DBRequest::DB_ENTRY_DELETE)  {
        //We need to delete the old sequence links as well
        // remove left->right entry
        if (left) {
            for(it = left->begin(); it != left->end(); it++) {
                if (((*it).node_key.id_type == key->right_key.id_type) &&
                    ((*it).node_key.id_name == key->right_key.id_name)) {
                    left->erase(it);
                    break;
                }
            }
            RemoveDefListEntry(&link_def_map_, left_it, NULL);
        }

        // remove right->left entry
        if (right) {
            for(it = right->begin(); it != right->end(); it++) {
                if (((*it).node_key.id_type == key->left_key.id_type) &&
                    ((*it).node_key.id_name == key->left_key.id_name)) {
                    right->erase(it);
                    break;
                }
            }
            RemoveDefListEntry(&link_def_map_, right_it, NULL);
        }

        return;
    }

    bool push_left = true;

    // Add/Update left->right entry
    if (left) {
        // If list already contains, just update the seq number
        for(it = left->begin(); it != left->end(); it++) {
            if (((*it).node_key.id_type == key->right_key.id_type) &&
                    ((*it).node_key.id_name == key->right_key.id_name)) {
                (*it).node_key.id_seq_num = key->right_key.id_seq_num;
                (*it).link_metadata = key->metadata;
                push_left = false;
                break;
            }
        }
    } else {
        left = new std::list<DeferredNode>();
        link_def_map_[key->left_key] = left;
    }

    bool push_right = true;
    // Add/Update right->left entry
    if (right) {
        // If list already contains, just update the seq number
        for(it = right->begin(); it != right->end(); it++) {
            if (((*it).node_key.id_type == key->left_key.id_type) &&
                    ((*it).node_key.id_name == key->left_key.id_name)) {
                (*it).node_key.id_seq_num = key->left_key.id_seq_num;
                (*it).link_metadata = key->metadata;
                push_right = false;
                break;
            }
        }
    } else {
        right = new std::list<DeferredNode>();
        link_def_map_[key->right_key] = right;
    }

    // Add it to the end of the list
    struct DeferredNode dn;
    dn.link_metadata = key->metadata;
    if (push_left) {
        dn.node_key = key->right_key;
        left->push_back(dn);
    }
    if (push_right) {
        dn.node_key = key->left_key;
        right->push_back(dn);
    }
    return;
}

void IFMapAgentTable::Input(DBTablePartition *partition, DBClient *client,
                             DBRequest *request) {
    RequestKey *key = static_cast<RequestKey *>(request->key.get());
    IFMapAgentTable *table = NULL;
    struct IFMapAgentData *req_data;
    IFMapObject *obj;

    table = TableFind(key->id_type);
    if (!table) {
        IFMAP_AGENT_TRACE(Trace, key->id_seq_num,
                "Table " + key->id_type + " not found");
        return;
    }

    IFMapNode *node = EntryLookup(key);
    if (table->pre_filter_) {
        DBRequest::DBOperation old_oper = request->oper;
        if (table->pre_filter_(table, node, request) == false) {
            IFMAP_AGENT_TRACE(Trace, key->id_seq_num,
                    "Node " + key->id_name + " neglected as filter"
                    + "suppressed");
            return;
        }
        if ((old_oper != DBRequest::DB_ENTRY_DELETE) &&
                (request->oper == DBRequest::DB_ENTRY_DELETE)) {
            IFMAP_AGENT_TRACE(Trace, key->id_seq_num,
                    "Node " + key->id_name + "ID_PERMS Null");
        }
    }

    if (request->oper == DBRequest::DB_ENTRY_DELETE) {
        if (node == NULL) {
            IFMAP_AGENT_TRACE(Trace, key->id_seq_num,
                    "Node " + key->id_name + " not found in Delete");
            return;
        }

        if (node->IsDeleted()) {
            IFMAP_AGENT_TRACE(Trace, key->id_seq_num,
                        "Node " + key->id_name + " already deleted");
            return;
        }

        obj = node->GetObject();
        //We dont accept lesser sequence number updates
        assert(obj->sequence_number() <= key->id_seq_num);

        //Upate the sequence number even for deletion of node
        obj->set_sequence_number(key->id_seq_num);
        DeleteNode(node);
        return;
    }

    if (request->oper == DBRequest::DB_ENTRY_NOTIFY) {
        if (node) {
            partition->Notify(node);
        }
        return;
    }

    node = EntryLocate(node, key);
    assert(node);

    //Get the data from request key and notify oper tables
    req_data = static_cast<struct IFMapAgentData *>(request->data.get());
    obj = static_cast<IFMapObject *>(req_data->content.release());

    //Set the sequence number of the object
    obj->set_sequence_number(key->id_seq_num);

    node->Insert(obj);
    NotifyNode(node);

    IFMapAgentLinkTable *link_table = static_cast<IFMapAgentLinkTable *>(
        database()->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    link_table->EvalDefLink(key);
}

void IFMapAgentTable::Clear() {
    assert(!HasListeners());
    DBTablePartition *partition = static_cast<DBTablePartition *>(
        GetTablePartition(0));
    IFMapNode *next = NULL;
    for (IFMapNode *node = static_cast<IFMapNode *>(partition->GetFirst());
         node != NULL; node = next) {
        next = static_cast<IFMapNode *>(partition->GetNext(node));
        if (node->IsDeleted()) {
            continue;
        }
        graph()->RemoveNode(node);
        partition->Delete(node);
    }
}


// Agent link table routines
IFMapLink *IFMapAgentLinkTable::FindLink(IFMapNode *left, IFMapNode *right,
                                  const std::string &metadata) {

    IFMapLinkTable *table = static_cast<IFMapLinkTable *>(
        database()->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(table != NULL);
    IFMapLink *link = table->FindLink(metadata, left, right);
    return (link ? (link->IsDeleted() ? NULL : link) : NULL);
}

void IFMapAgentLinkTable::AddLink(IFMapNode *left, IFMapNode *right,
                                  const std::string &metadata,
                                  uint64_t seq) {

    IFMapLinkTable *table = static_cast<IFMapLinkTable *>(
        database()->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(table != NULL);

    IFMapLink *link = table->AddLink(left, right, metadata, seq,
                                     IFMapOrigin(IFMapOrigin::UNKNOWN));
    graph()->Link(left, right, (DBGraphEdge *)link);
}

void IFMapAgentLinkTable::DelLink(IFMapNode *left, IFMapNode *right, DBGraphEdge *edge) {
    IFMapAgentLinkTable *table = static_cast<IFMapAgentLinkTable *>(
        database()->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    assert(table != NULL);
    table->DeleteLink(static_cast<IFMapLink *>(edge));
}

IFMapAgentLinkTable::IFMapAgentLinkTable(DB *db, const string &name, DBGraph *graph)
        : IFMapLinkTable(db, name, graph) {
}

DBTable *IFMapAgentLinkTable::CreateTable(DB *db, const string &name,
                                     DBGraph *graph) {
    IFMapAgentLinkTable *table = new IFMapAgentLinkTable(db, name, graph);
    table->Init();
    return table;
}


void IFMapAgentLinkTable_Init(DB *db, DBGraph *graph) {
    db->RegisterFactory(IFMAP_AGENT_LINK_DB_NAME,
        boost::bind(&IFMapAgentLinkTable::CreateTable, _1, _2, graph));
    db->CreateTable(IFMAP_AGENT_LINK_DB_NAME);
}

void IFMapAgentLinkTable::Input(DBTablePartition *partition, DBClient *client,
                           DBRequest *req) {

    RequestKey *key = static_cast<RequestKey *>(req->key.get());

    IFMapNode *left;
    left = IFMapAgentTable::TableEntryLookup(database(), &key->left_key);
    if (!left) {
        IFMAP_AGENT_TRACE(Trace, key->left_key.id_seq_num,
                key->left_key.id_type + ":" + key->left_key.id_name +
                " not present for link to " + key->right_key.id_type +
                ":" + key->right_key.id_name);
        LinkDefAdd(req);
        return;
    }

    IFMapNode *right;
    right = IFMapAgentTable::TableEntryLookup(database(), &key->right_key);
    if (!right) {
        IFMAP_AGENT_TRACE(Trace, key->left_key.id_seq_num,
                key->right_key.id_type + " : " + key->right_key.id_name +
                " not present for link to " + key->left_key.id_type + " : " +
                key->left_key.id_name);
        LinkDefAdd(req);
        return;
    }

    if (left->IsDeleted()) {
        IFMAP_AGENT_TRACE(Trace, key->left_key.id_seq_num,
            "Adding Link" + key->left_key.id_type + ":" +
            key->left_key.id_name + "->" + key->right_key.id_type +
                            ":" + key->right_key.id_name + " to defer "
                            "list as left is deleted marked");
        LinkDefAdd(req);
        return;
    }

    if (right->IsDeleted()) {
        IFMAP_AGENT_TRACE(Trace, key->left_key.id_seq_num,
            "Adding Link" + key->left_key.id_type + ":" +
            key->left_key.id_name + "->" + key->right_key.id_type +
                            ":" + key->right_key.id_name + " to defer "
                            "list as right is deleted marked");
        LinkDefAdd(req);
        return;
    }

    IFMapObject *obj = left->GetObject();
    if (obj->sequence_number() < key->left_key.id_seq_num) {
        IFMAP_AGENT_TRACE(Trace, key->left_key.id_seq_num,
            "IFMap Link " + left->name() + right->name() +
            " with wrong seq number");
        LinkDefAdd(req);
        return;
    }

    obj = right->GetObject();
    if (obj->sequence_number() < key->left_key.id_seq_num) {
        IFMAP_AGENT_TRACE(Trace, key->left_key.id_seq_num,
            "IFMap Link " + left->name() + right->name() +
            " with wrong seq number");
        LinkDefAdd(req);
        return;
    }

    DBGraphEdge *link = FindLink(left, right, key->metadata);

    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        if (link == NULL) {
            AddLink(left, right, key->metadata, key->left_key.id_seq_num);
        } else {
            IFMapOrigin origin(IFMapOrigin::UNKNOWN);
            IFMapLink *l = static_cast<IFMapLink *>(link);
            l->UpdateProperties(origin, key->left_key.id_seq_num);
        }
    } else {
        if (link == NULL) {
            return;
        }
        DelLink(left, right, link);
    }
}

bool IFMapAgentLinkTable::RemoveDefListEntry
    (LinkDefMap *map, LinkDefMap::iterator &map_it,
     std::list<DeferredNode>::iterator *list_it) {

    std::list<DeferredNode> *list = map_it->second;
    if (list_it) {
        list->erase(*list_it);
    }

    if (list->size()) {
        return false;
    }
    map->erase(map_it);
    delete list;
    return true;
}

// For every link there are 2 entries,
//  left->right
//  right->left
//
//  If both left and right node are available, remove the entries and try to
//  add the link
void IFMapAgentLinkTable::EvalDefLink(IFMapTable::RequestKey *key) {
    LinkDefMap::iterator link_defmap_it = link_def_map_.find(*key);
    if (link_def_map_.end() == link_defmap_it)
        return;

    std::list<DeferredNode> *left_list = link_defmap_it->second;
    std::list<DeferredNode>::iterator left_it, left_list_entry;
    for(left_it = left_list->begin(); left_it != left_list->end();) {
        left_list_entry = left_it++;

        // If link seq is older, dont consider the link.
        if ((*left_list_entry).node_key.id_seq_num < key->id_seq_num)
            continue;

        // Skip if right-node is not yet present
        IFMapNode *node = IFMapAgentTable::TableEntryLookup(database(),
                    &((*left_list_entry).node_key));
        if (!node)
            continue;

        //If the other end of the node is not from active control node,
        //dont consider the link
        IFMapObject *obj = node->GetObject();
        if (obj->sequence_number() < key->id_seq_num)
            continue;


        // left->right entry found defer-list. Find the right->left entry
        LinkDefMap::iterator right_defmap_it =
            link_def_map_.find((*left_list_entry).node_key);
        assert(link_def_map_.end() != right_defmap_it);

        std::list<DeferredNode> *right_list = right_defmap_it->second;
        std::list<DeferredNode>::iterator right_it, right_list_entry;
        for(right_it = right_list->begin(); right_it !=
                right_list->end(); right_it++) {

            // If link seq is older, dont consider the link.
            if ((*right_it).node_key.id_seq_num < key->id_seq_num)
                continue;

            if ((*right_it).node_key.id_type == key->id_type &&
                    (*right_it).node_key.id_name == key->id_name) {
                RemoveDefListEntry(&link_def_map_, right_defmap_it,
                               &right_it);
                break;
            }
        }

        //We should have removed something in the above iteration
        assert(right_it != right_list->end());

        //Remove from deferred list before enqueing
        auto_ptr <RequestKey> req_key (new RequestKey);
        req_key->left_key = *key;
        req_key->right_key = (*left_list_entry).node_key;
        req_key->metadata = (*left_list_entry).link_metadata;
        // Dont delete left_list_entry. Its passed in req structure
        left_list->erase(left_list_entry);

        DBRequest req;
        req.key = req_key;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        Enqueue(&req);
    }

    // If list does not have any entries, delete the list
    RemoveDefListEntry(&link_def_map_, link_defmap_it, NULL);
}

void IFMapAgentLinkTable::DestroyDefLink(uint64_t seq) {
    std::list<DeferredNode> *ent;
    std::list<DeferredNode>::iterator it, list_entry;
    IFMapAgentLinkTable::LinkDefMap::iterator dlist_it, temp;

    for(dlist_it = link_def_map_.begin(); dlist_it != link_def_map_.end(); ) {
        temp = dlist_it++;
        ent = temp->second;
        for(it = ent->begin(); it != ent->end();) {
            list_entry = it++;

            //Delete the deferred link if it is old seq
            if ((*list_entry).node_key.id_seq_num < seq) {
                if (RemoveDefListEntry(&link_def_map_, temp,
                            &list_entry) == true) {
                    //The list has been deleted. Move to the next map
                    //entry
                    break;
                }
            }
        }
    }
}

//Stale Cleaner functionality
class IFMapAgentStaleCleaner::IFMapAgentStaleCleanerWorker : public Task {
public:

    IFMapAgentStaleCleanerWorker(DB *db, DBGraph *graph, uint64_t seq):
        Task(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0),
        db_(db), graph_(graph), seq_(seq) {
    }

    bool Run() {
        IFMAP_AGENT_TRACE(Trace, seq_,
                "IFMap Config Audit start:");
        //Handle the links first
        DBGraph::edge_iterator e_next(graph_);
        for (DBGraph::edge_iterator e_iter = graph_->edge_list_begin();
            e_iter != graph_->edge_list_end(); e_iter = e_next) {

            const DBGraph::DBEdgeInfo &tuple = *e_iter;

            e_next = ++e_iter;

            IFMapNode *lhs = static_cast<IFMapNode *>(boost::get<0>(tuple));
            IFMapNode *rhs = static_cast<IFMapNode *>(boost::get<1>(tuple));
            IFMapLink *link = static_cast<IFMapLink *>(boost::get<2>(tuple));
            assert(link);

            bool exists = false;
            IFMapLink::LinkOriginInfo origin_info =
                link->GetOriginInfo(IFMapOrigin::UNKNOWN, &exists);
            if (exists && (origin_info.sequence_number < seq_ )) {
                IFMapAgentLinkTable *ltable = static_cast<IFMapAgentLinkTable *>(
                    db_->FindTable(IFMAP_AGENT_LINK_DB_NAME));
                IFMAP_AGENT_TRACE(Trace,
                     origin_info.sequence_number, "Deleting Link between " +
                     lhs->name() + rhs->name());
                ltable->DeleteLink(link);
            }
        }

        //Handle the vertices now
        DBGraph::vertex_iterator v_next(graph_);
        for (DBGraph::vertex_iterator v_iter = graph_->vertex_list_begin();
            v_iter != graph_->vertex_list_end(); v_iter = v_next) {

            IFMapNode *node = static_cast<IFMapNode *>(v_iter.operator->());
            v_next = ++v_iter;

            IFMapObject *obj = node->GetObject();
            assert(obj);
            if (obj->sequence_number() < seq_) {
                IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
                IFMAP_AGENT_TRACE(Trace, obj->sequence_number(),
                        "Deleting node " + node->name());
                table->DeleteNode(node);
            }
        }

        //Handle deferred list
        IFMapAgentLinkTable *table = static_cast<IFMapAgentLinkTable *>(
                    db_->FindTable(IFMAP_AGENT_LINK_DB_NAME));
        table->DestroyDefLink(seq_);

        return true;
    }
    std::string Description() const {
        return "IFMapAgentStaleCleaner::IFMapAgentStaleCleanerWorker";
    }

private:
    DB *db_;
    DBGraph *graph_;
    uint64_t seq_;
};

IFMapAgentStaleCleaner::~IFMapAgentStaleCleaner() {
}

IFMapAgentStaleCleaner::IFMapAgentStaleCleaner(DB *db, DBGraph *graph) :
        db_(db), graph_(graph) {
}

bool IFMapAgentStaleCleaner::StaleTimeout(uint64_t seq) {
    seq_ = seq;
    IFMapAgentStaleCleanerWorker *cleaner = new IFMapAgentStaleCleanerWorker(db_, graph_, seq_);
    TaskScheduler *sch = TaskScheduler::GetInstance();
    sch->Enqueue(cleaner);
    return false;
}

void IFMapAgentStaleCleaner::Clear() {
    IFMapLinkTable *table = static_cast<IFMapLinkTable *>(
        db_->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    table->Clear();
    IFMapTable::ClearTables(db_);
}
