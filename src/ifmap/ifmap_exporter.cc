/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_exporter.h"

#include <boost/bind.hpp>
#include <boost/checked_delete.hpp>

#include "db/db.h"
#include "db/db_table_partition.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_graph_walker.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_update_queue.h"
#include "ifmap/ifmap_update_sender.h"

using namespace std;

class IFMapExporter::TableInfo {
public:
    TableInfo(DBTable::ListenerId id)
            : id_(id) {
    }
    DBTableBase::ListenerId id() const { return id_; }

private:
    DBTableBase::ListenerId id_;
};

IFMapExporter::IFMapExporter(IFMapServer *server)
        : server_(server), link_table_(NULL) {
}

IFMapExporter::~IFMapExporter() {
    Shutdown();
}

void IFMapExporter::Initialize(DB *db) {
    for (DB::iterator iter = db->lower_bound("__ifmap__");
         iter != db->end(); ++iter) {
        DBTable *table = static_cast<DBTable *>(iter->second);
        if (table->name().find("__ifmap__") != 0) {
            break;
        }
        DBTable::ListenerId id =
                table->Register(
                    boost::bind(&IFMapExporter::NodeTableExport, this, _1, _2));
        table_map_.insert(make_pair(table, new TableInfo(id)));
    }

    link_table_ = static_cast<DBTable *>(
        db->FindTable("__ifmap_metadata__.0"));
    assert(link_table_);
    DBTable::ListenerId id =
            link_table_->Register(
                boost::bind(&IFMapExporter::LinkTableExport, this, _1, _2));
    table_map_.insert(make_pair(link_table_, new TableInfo(id)));

    walker_.reset(new IFMapGraphWalker(server_->graph(), this));
}

void IFMapExporter::Shutdown() {
    for (TableMap::iterator iter = table_map_.begin(); iter != table_map_.end();
         ++iter) {
        DBTable *table = iter->first;
        TableInfo *info = iter->second;
        table->Unregister(info->id());
        TableStateClear(table, info->id());
        delete info;
    }
    table_map_.clear();
}

const IFMapExporter::TableInfo *IFMapExporter::Find(
    const DBTable *table) const {
    TableMap::const_iterator loc =
            table_map_.find(const_cast<DBTable *>(table));
    if (loc != table_map_.end()) {
        return loc->second;
    }
    return NULL;
}

DBTableBase::ListenerId IFMapExporter::TableListenerId(
    const DBTable *table) const {
    const IFMapExporter::TableInfo *tinfo = Find(table);
    if (tinfo == NULL) {
        return DBTableBase::kInvalidId;
    }
    return tinfo->id();
}

bool IFMapExporter::IsFeasible(const IFMapNode *node) {
    if (node->IsDeleted()) {
        return false;
    }
    return true;
}

const BitSet *IFMapExporter::MergeClientInterest(
    IFMapNode * node, IFMapNodeState *state, std::auto_ptr<BitSet> *ptr) {

    const BitSet *set = &state->interest();
    IFMapTable *table = node->table();

    if (table->name() == "__ifmap__.virtual_router.0") {
        IFMapClient *client = server_->FindClient(node->name());
        if (!client) {
            return set;
        }
        BitSet *merged_set = new BitSet(*set);
        merged_set->set(client->index());
        ptr->reset(merged_set);
        state->SetInterest(*merged_set);
        return merged_set;
    } else if (table->name() == "__ifmap__.virtual_network.0") {
        // TODO: merge the bits corresponding to the virtual networks that
        // clients have subscribed for.
    }

    return set;
}

IFMapNodeState *IFMapExporter::NodeStateLookup(IFMapNode *node){
    const TableInfo *tinfo = Find(node->table());
    IFMapNodeState *state = static_cast<IFMapNodeState *>(
        node->GetState(node->table(), tinfo->id()));
    return state;
}

IFMapNodeState *IFMapExporter::NodeStateLocate(IFMapNode *node){
    const TableInfo *tinfo = Find(node->table());
    IFMapNodeState *state = static_cast<IFMapNodeState *>(
        node->GetState(node->table(), tinfo->id()));
    if (state == NULL) {
        state = new IFMapNodeState();
        node->SetState(node->table(), tinfo->id(), state);
    }
    return state;
}

IFMapUpdateQueue *IFMapExporter::queue() {
    return server_->queue();
}

IFMapUpdateSender *IFMapExporter::sender() {
    return server_->sender();
}

template <class ObjectType>
bool IFMapExporter::UpdateAddChange(ObjectType *obj, IFMapState *state,
                                    const BitSet &add_set, const BitSet &rm_set,
                                    bool change) {
    // Remove any bit in "advertise" from the positive update.
    // This is a NOP in case the interest set is non empty and this is change.
    IFMapUpdate *update = state->GetUpdate(IFMapListEntry::UPDATE);
    if (update != NULL) {
        update->AdvertiseReset(rm_set);
    }

    if (state->interest().empty()) {
        if (update != NULL) {
            queue()->Dequeue(update);
            state->Remove(update);
            delete update;
        }
        return false;
    }

    if (!change && add_set.empty()) {
        return false;
    }

    bool is_move = false;
    if (update != NULL) {
        if (!change) {
            if (update->advertise().Contains(add_set)) {
                return false;
            }
        } else {
            if (state->interest() == update->advertise()) {
                return false;
            }
        }
        is_move = true;
        queue()->Dequeue(update);
    } else {
        update = new IFMapUpdate(obj, true);
        state->Insert(update);
    }

    if (!change) {
        update->AdvertiseOr(add_set);
    } else {
        update->SetAdvertise(state->interest());
    }
    bool tm_last = queue()->Enqueue(update);
    // If the tail_marker was the last element before the enqueue, send a
    // trigger to the sender to create a task to do the 'send'.
    if (tm_last) {
        sender()->QueueActive();
    }
    return is_move;
}

template <class ObjectType>
bool IFMapExporter::UpdateRemove(ObjectType *obj, IFMapState *state,
                                 const BitSet &rm_set) {
    // Remove any bit in "interest" from the delete update.
    IFMapUpdate *update = state->GetUpdate(IFMapListEntry::DELETE);
    if (update != NULL) {
        update->AdvertiseReset(state->interest());
    }

    if (rm_set.empty()) {
        if (update != NULL) {
            queue()->Dequeue(update);
            state->Remove(update);
            delete update;
        }
        return false;
    }

    bool is_move = false;
    if (update != NULL) {
        if (rm_set == update->advertise()) {
            return false;
        }
        is_move = true;
        queue()->Dequeue(update);
    } else {
        update = new IFMapUpdate(obj, false);
        state->Insert(update);
    }
    
    update->SetAdvertise(rm_set);
    bool tm_last = queue()->Enqueue(update);
    // If the tail_marker was the last element before the enqueue, send a
    // trigger to the sender to create a task to do the 'send'.
    if (tm_last) {
        sender()->QueueActive();
    }
    return is_move;
}

template <class ObjectType>
void IFMapExporter::EnqueueDelete(ObjectType *obj, IFMapState *state) {
    IFMapUpdate *update = state->GetUpdate(IFMapListEntry::UPDATE);
    if (update != NULL) {
        queue()->Dequeue(update);
        state->Remove(update);
        delete update;        
    }

    update = state->GetUpdate(IFMapListEntry::DELETE);
    if (update != NULL) {
        queue()->Dequeue(update);
    }
    if (state->advertised().empty()) {
        assert(update == NULL);
        return;
    }

    if (update == NULL) {
        update = new IFMapUpdate(obj, false);
        state->Insert(update);
    }
    update->SetAdvertise(state->advertised());
    bool was_idle = queue()->Enqueue(update);
    if (was_idle) {
        sender()->QueueActive();
    }
}

IFMapLinkState *IFMapExporter::LinkStateLookup(IFMapLink *link) {
    const TableInfo *tinfo = Find(link_table_);
    IFMapLinkState *state = static_cast<IFMapLinkState *>(
        link->GetState(link_table_, tinfo->id()));
    return state;    
}

void IFMapExporter::MoveDependentLinks(IFMapNodeState *state) {
    for (IFMapNodeState::iterator iter = state->begin(); iter != state->end();
         ++iter) {
        IFMapLink *link = iter.operator->();
        IFMapLinkState *ls = LinkStateLookup(link);
        if (ls == NULL) {
            continue;
        }
        IFMapUpdate *update = state->GetUpdate(IFMapListEntry::UPDATE);
        if (update == NULL) {
            continue;
        }
        assert(!update->advertise().empty());
        queue()->Dequeue(update);
        queue()->Enqueue(update);
    }
}

void IFMapExporter::MoveAdjacentNode(IFMapNodeState *state) {
    IFMapUpdate *update = state->GetUpdate(IFMapListEntry::DELETE);
    if (update != NULL) {
        assert(!update->advertise().empty());
        queue()->Dequeue(update);
        queue()->Enqueue(update);
    }
}

void IFMapExporter::RemoveDependentLinks(DBTablePartBase *partition,
                                         IFMapNodeState *state,
                                         const BitSet &rm_set) {
    for (IFMapNodeState::iterator iter = state->begin(), next = state->begin();
         iter != state->end(); iter = next) {
        IFMapLink *link = iter.operator->();
        next = ++iter;
        IFMapLinkState *ls = LinkStateLookup(link);
        if (ls == NULL) {
            continue;
        }
        BitSet common = ls->advertised() & rm_set;
        if (!common.empty()) {
            LinkTableExport(partition, link);
        }
    }
}

void IFMapExporter::ProcessAdjacentNode(
    DBTablePartBase *partition, IFMapNode *node, const BitSet &add_set,
    IFMapNodeState *state) {
    BitSet current = state->advertised();
    IFMapUpdate *update = state->GetUpdate(IFMapListEntry::UPDATE);
    if (update) {
        current |= update->advertise();
    }
    if (!current.Contains(add_set)) {
        NodeTableExport(partition, node);
    }
}

// Propagate changes to all the interested peers.
//
// Update order:
// link updates (adds) should only be advertised after the corresponding nodes
// are advertised.
// node membership removal (deletes) should only be advertised after all the
// refering links are removed.
// When enqueuing a link add, the code forces node processing of adjacent links
// before the link update is added to the queue.
// When enqueueing a node removal, the corresponding link removals are placed
// in the queue before the node.
// When a node update moves, any dependent (positive) link update moves also.
// When a (negative) link update moves the corresponding node removals move
// also.
void IFMapExporter::NodeTableExport(DBTablePartBase *partition,
                                    DBEntryBase *entry) {
    IFMapNode *node = static_cast<IFMapNode *>(entry);
    DBTable *table = static_cast<DBTablePartition *>(partition)->table();

    const TableInfo *tinfo = Find(table);
    DBState *entry_state = entry->GetState(table, tinfo->id());
    IFMapNodeState *state = static_cast<IFMapNodeState *>(entry_state);

    if (IsFeasible(node)) {
        if (state == NULL) {
            state = new IFMapNodeState();
            entry->SetState(table, tinfo->id(), state);
        }
        state->SetValid(node);

        // This is an add operation for nodes that are interested and
        // have not seen the advertisement.
        BitSet add_set;
        add_set.BuildComplement(state->interest(), state->advertised());

        // This is a delete operation for nodes that have seen it but are no
        // longer interested.
        BitSet rm_set;
        rm_set.BuildComplement(state->advertised(), state->interest());

        // TODO: detect a change in the record.
        bool change = true;
        
        // enqueue update
        // If there is a previous update in the queue, if that update has
        // been seen by any of receivers, we need to move the update to
        // the tail of the list. When that happens, dependent updates
        // moved also.
        bool move = UpdateAddChange(node, state, add_set, rm_set, change);
        if (move) {
            MoveDependentLinks(state);
        }

        // For the subset of clients being removed, make sure that all
        // dependent links are removed before.
        if (!rm_set.empty()) {
            RemoveDependentLinks(partition, state, rm_set);
        }
        UpdateRemove(node, state, rm_set);
    } else if (state != NULL) {
        // Link deletes must preceed node deletes.
        state->ClearValid();
        if (!state->HasDependents()) {
            // enqueue delete.
            EnqueueDelete(node, state);
            if (state->update_list().empty()) {
                entry->ClearState(table, tinfo->id());
                delete state;
            }
        }
    }
}

static void MaybeNotifyOnLinkDelete(IFMapNode *node, IFMapNodeState *state) {
    if (node->IsDeleted() && !state->HasDependents()) {
        IFMapTable *table = node->table();
        table->Change(node);
    }
}

// When a link is created or deleted this may affect the interest graph for
// the agents.
// Link changes should only be propagated after the respective nodes are
// feasible.
void IFMapExporter::LinkTableExport(DBTablePartBase *partition,
                                    DBEntryBase *entry) {
    IFMapLink *link = static_cast<IFMapLink *>(entry);
    DBTable *table = static_cast<DBTablePartition *>(partition)->table();
    const TableInfo *tinfo = Find(table);
    DBState *entry_state = entry->GetState(table, tinfo->id());
    IFMapLinkState *state = static_cast<IFMapLinkState *>(entry_state);

    if (!entry->IsDeleted()) {
        IFMapNodeState *s_left = NULL;
        IFMapNodeState *s_right = NULL;

        bool add_link = false;
        if (state == NULL) {
            state = new IFMapLinkState(link);
            entry->SetState(table, tinfo->id(), state);
            s_left = NodeStateLocate(link->left());
            s_right = NodeStateLocate(link->right());
            add_link = true;
        } else {
            assert(state->HasDependency());
            s_left = state->left();
            s_right = state->right();
        }

        // If one of the nodes is a vswitch node, then the interest mask
        // is the corresponding peer bit.
        std::auto_ptr<BitSet> ml, mr;
        const BitSet *lset = MergeClientInterest(link->left(), s_left, &ml);
        const BitSet *rset = MergeClientInterest(link->right(), s_right, &mr);
        if (*lset != *rset) {
            walker_->LinkAdd(link->left(), *lset, link->right(), *rset);
        }

        if (add_link) {
            // Establish dependency.
            state->SetDependency(s_left, s_right);
            state->SetValid();
        }

        if (IsFeasible(link->left()) && IsFeasible(link->right())) {
            // Interest mask is the intersection of left and right nodes.
            state->SetInterest(s_left->interest() & s_right->interest());
        } else {
            state->SetInterest(BitSet());
        }

        // This is an add operation for nodes that are interested and
        // have not seen the advertisement.
        BitSet add_set;
        add_set.BuildComplement(state->interest(), state->advertised());

        BitSet rm_set;
        rm_set.BuildComplement(state->advertised(), state->interest());

        if (!add_set.empty()) {
            ProcessAdjacentNode(partition, link->left(), add_set, s_left);
            ProcessAdjacentNode(partition, link->right(), add_set, s_right);
        }

        UpdateAddChange(link, state, add_set, rm_set, false);
            
        bool move = UpdateRemove(link, state, rm_set);
        if (move) {
            MoveAdjacentNode(s_left);
            MoveAdjacentNode(s_right);
        }
    } else if ((state != NULL) && state->IsValid()) {
        IFMapNode *left = link->LeftNode(server_->database());
        IFMapNodeState *s_left = state->left();
        assert((left != NULL) && (s_left != NULL));
        IFMapNode *right = link->RightNode(server_->database());
        IFMapNodeState *s_right = state->right();
        assert((right != NULL) && (s_right != NULL));
        BitSet interest = s_left->interest() & s_right->interest();
        IFMAP_DEBUG(LinkOper, "LinkRemove", left->ToString(), right->ToString(),
            s_left->interest().ToString(), s_right->interest().ToString());
        walker_->LinkRemove(interest);

        state->RemoveDependency();
        state->ClearValid();

        // enqueue update.
        EnqueueDelete(link, state);
        if (state->update_list().empty()) {
            entry->ClearState(table, tinfo->id());
            delete state;            
        }

        MaybeNotifyOnLinkDelete(left, s_left);
        MaybeNotifyOnLinkDelete(right, s_right);
    }
}

void IFMapExporter::StateUpdateOnDequeue(IFMapUpdate *update,
                                         const BitSet &dequeue_set,
                                         bool is_delete) {
    DBTable *table = NULL;
    DBEntry *db_entry = NULL;

    IFMapState *state = NULL;
    if (update->data().type == IFMapObjectPtr::NODE) {
        IFMapNode *node = update->data().u.node;
        db_entry = node;
        table = node->table();
    } else if (update->data().type == IFMapObjectPtr::LINK) {
        db_entry = update->data().u.link;
        table = link_table_;
    }
    state = static_cast<IFMapState *>(
        db_entry->GetState(table, TableListenerId(table)));
    if (is_delete) {
        state->AdvertisedReset(dequeue_set);
    } else {
        state->AdvertisedOr(dequeue_set);
    }
    if (update->advertise().empty()) {
        state->Remove(update);
        if (state->update_list().empty() && state->IsInvalid()) {
            assert(state->advertised().empty());
            db_entry->ClearState(table, TableListenerId(table));
            delete state;
        }
        delete update;
    }
}

struct IFMapUpdateDisposer {
    explicit IFMapUpdateDisposer(IFMapUpdateQueue *queue) : queue_(queue) { }
    void operator()(IFMapUpdate *ptr) {
        queue_->Dequeue(ptr);
        boost::checked_delete(ptr);
    }

  private:
    IFMapUpdateQueue *queue_;
};

void IFMapExporter::TableStateClear(DBTable *table,
                                    DBTable::ListenerId tsid) {
    DBTablePartition *partition = static_cast<DBTablePartition *>(
        table->GetTablePartition(0));

    IFMapUpdateDisposer disposer(queue());
    for (DBEntry *entry = static_cast<DBEntry *>(partition->GetFirst()),
                 *next = NULL; entry != NULL; entry = next) {
        next = static_cast<DBEntry *>(partition->GetNext(entry));
        IFMapState *state = static_cast<IFMapState *>(
            entry->GetState(table, tsid));
        if (state == NULL) {
            continue;
        }
        entry->ClearState(table, tsid);
        state->ClearAndDispose(disposer);
        boost::checked_delete(state);
    }

}

bool IFMapExporter::FilterNeighbor(IFMapNode *lnode, IFMapNode *rnode) {
    return walker_->FilterNeighbor(lnode, rnode);
}

