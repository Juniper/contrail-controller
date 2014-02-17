/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "ifmap/ifmap_server.h"

#include <boost/asio/io_service.hpp>
#include <boost/algorithm/string.hpp>

#include "base/logging.h"
#include "bgp/bgp_sandesh.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "db/db_graph_edge.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_graph_walker.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_update_queue.h"
#include "ifmap/ifmap_update_sender.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/ifmap_uuid_mapper.h"
#include "schema/vnc_cfg_types.h"

using std::make_pair;

class IFMapServer::IFMapStaleCleaner : public Task {
public:
    IFMapStaleCleaner(DB *db, DBGraph *graph, IFMapServer *server):
        Task(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0), 
        db_(db), graph_(graph), ifmap_server_(server) {
    }

    bool Run() {
        // objects_deleted indicates the count of objects-deleted-but-not-node
        uint32_t nodes_deleted = 0, nodes_changed = 0, links_deleted = 0,
                 objects_deleted = 0;
        uint64_t curr_seq_num =
            ifmap_server_->get_ifmap_channel_sequence_number();

        DBGraph::edge_iterator e_next(graph_);
        for (DBGraph::edge_iterator e_iter = graph_->edge_list_begin();
             e_iter != graph_->edge_list_end(); e_iter = e_next) {

            const DBGraph::DBVertexPair &tuple = *e_iter;
            // increment only after dereferencing
            e_next = ++e_iter;

            IFMapNode *lhs = static_cast<IFMapNode *>(tuple.first);
            IFMapNode *rhs = static_cast<IFMapNode *>(tuple.second);

            IFMapLink *link =
                static_cast<IFMapLink *>(graph_->GetEdge(lhs, rhs));
            assert(link);

            bool exists = false;
            IFMapLink::LinkOriginInfo origin_info = 
                link->GetOriginInfo(IFMapOrigin::MAP_SERVER, &exists);
            if (exists && (origin_info.sequence_number < curr_seq_num)) {
                IFMapLinkTable *ltable = static_cast<IFMapLinkTable *>(
                    db_->FindTable("__ifmap_metadata__.0"));
                // Cleanup the node and remove from the graph
                link->RemoveOriginInfo(IFMapOrigin::MAP_SERVER);
                if (link->is_origin_empty()) {
                    ltable->DeleteLink(link, lhs, rhs);
                }
                links_deleted++;
            }
        }

        DBGraph::vertex_iterator v_next(graph_);
        for (DBGraph::vertex_iterator v_iter = graph_->vertex_list_begin();
            v_iter != graph_->vertex_list_end(); v_iter = v_next) {

            IFMapNode *node = static_cast<IFMapNode *>(v_iter.operator->());
            // increment only after dereferencing
            v_next = ++v_iter;

            IFMapObject *object = 
                node->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
            IFMapServerTable *ntable =
                static_cast<IFMapServerTable *>(node->table());
            if (object != NULL) {
                if (object->sequence_number() < curr_seq_num) {
                    node->Remove(object);
                    bool retb = ntable->DeleteIfEmpty(node);
                    if (retb) {
                        nodes_deleted++;
                    } else {
                        objects_deleted++;
                    }
                } else {
                    // There could be stale properties
                    bool changed = object->ResolveStaleness();
                    if (changed) {
                        nodes_changed++;
                        ntable->Notify(node);
                    }
                }
            } else {
                // The node doesnt have any object. We should delete it if it
                // does not have any neighbors either.
                bool retb = ntable->DeleteIfEmpty(node);
                if (retb) {
                    nodes_deleted++;
                }
            }
        }
        IFMAP_DEBUG(IFMapStaleCleanerInfo, curr_seq_num, nodes_deleted,
                    nodes_changed, links_deleted, objects_deleted);

        return true;
    }

private:
    DB *db_;
    DBGraph *graph_;
    IFMapServer *ifmap_server_;
};

class IFMapServer::IFMapVmSubscribe : public Task {
public:
    IFMapVmSubscribe(DB *db, DBGraph *graph, IFMapServer *server,
                     const std::string &vr_name, const std::string &vm_uuid,
                     bool subscribe, bool has_vms):
            Task(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0), 
            db_(db), ifmap_server_(server), vr_name_(vr_name),
            vm_uuid_(vm_uuid), subscribe_(subscribe), has_vms_(has_vms) {
    }

    bool Run() {

        IFMapServerTable *vm_table = static_cast<IFMapServerTable *>(
            db_->FindTable("__ifmap__.virtual_machine.0"));
        assert(vm_table != NULL);

        // Find the vm's node using its UUID. If the config has not added the
        // vm yet, treat this request as pending since we cant process it right
        // now.
        IFMapNode *vm_node = ifmap_server_->GetVmNodeByUuid(vm_uuid_);
        if (vm_node) {
            IFMapServerTable *vr_table = static_cast<IFMapServerTable *>(
                db_->FindTable("__ifmap__.virtual_router.0"));
            assert(vr_table != NULL);

            std::string vm_name = vm_node->name();
            vr_table->IFMapVmSubscribe(vr_name_, vm_name, subscribe_, has_vms_);

            IFMapClient *client = ifmap_server_->FindClient(vr_name_);
            if (client) {
                if (subscribe_) {
                    ifmap_server_->ClientGraphDownload(client);
                }
            }
        } else {
            ifmap_server_->ProcessVmRegAsPending(vm_uuid_, vr_name_,
                                                 subscribe_);
        }

        return true;
    }

private:
    DB *db_;
    IFMapServer *ifmap_server_;
    std::string vr_name_;
    std::string vm_uuid_;
    bool subscribe_;
    bool has_vms_;
};

IFMapServer::IFMapServer(DB *db, DBGraph *graph,
                         boost::asio::io_service *io_service)
        : db_(db), graph_(graph),
          queue_(new IFMapUpdateQueue(this)),
          exporter_(new IFMapExporter(this)),
          sender_(new IFMapUpdateSender(this, queue())),
          vm_uuid_mapper_(new IFMapVmUuidMapper(db_, this)),
          work_queue_(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                      boost::bind(&IFMapServer::ClientWorker, this, _1)),
          io_service_(io_service),
          stale_cleanup_timer_(TimerManager::CreateTimer(*(io_service_),
                                         "Stale cleanup timer")),
          ifmap_manager_(NULL), ifmap_channel_manager_(NULL) {
}

IFMapServer::~IFMapServer() {
}

void IFMapServer::Initialize() {
    exporter_->Initialize(db_);
    vm_uuid_mapper_->Initialize();
}

void IFMapServer::Shutdown() {
    TimerManager::DeleteTimer(stale_cleanup_timer_);
    vm_uuid_mapper_->Shutdown();
    exporter_->Shutdown();
}

void IFMapServer::ClientRegister(IFMapClient *client) {
    IFMAP_DEBUG(IFMapServerClientRegUnreg, "Register request for client ",
                client->identifier());
    size_t index = client_indexes_.find_first_clear();
    if (index == BitSet::npos) {
        index = client_indexes_.size();
    }
    client_indexes_.set(index);

    client_map_.insert(make_pair(client->identifier(), client));
    index_map_.insert(make_pair(index, client));
    client->Initialize(exporter_.get(), index);
    queue_->Join(index);
}

void IFMapServer::ClientUnregister(IFMapClient *client) {
    IFMAP_DEBUG(IFMapServerClientRegUnreg, "Un-register request for client ",
                client->identifier());
    size_t index = client->index();
    sender_->CleanupClient(index);
    queue_->Leave(index);
    index_map_.erase(index);
    client_map_.erase(client->identifier());
    client_indexes_.reset(index);
}

bool IFMapServer::ProcessClientWork(bool add, IFMapClient *client) {
    if (add) {
        ClientRegister(client);
    } else {
        ClientGraphCleanup(client);
        RemoveSelfAddedLinksAndObjects(client);
        CleanupUuidMapper(client);
        ClientUnregister(client);
    }
    return true;
}

// To be used only by tests.
void IFMapServer::AddClient(IFMapClient *client) {
    // Let ClientWorker() do all the work in the context of the db-task
    QueueEntry entry;
    entry.op = ADD;
    entry.client = client;
    work_queue_.Enqueue(entry);
}

// To be used only by tests.
void IFMapServer::DeleteClient(IFMapClient *client) {
    // Let ClientWorker() do all the work in the context of the db-task
    QueueEntry entry;
    entry.op = DELETE;
    entry.client = client;
    work_queue_.Enqueue(entry);
}

// To be used only by tests.
void IFMapServer::SimulateDeleteClient(IFMapClient *client) {
    QueueEntry entry;
    entry.op = DELETE;
    entry.client = client;
    ClientWorker(entry);
}

bool IFMapServer::ClientWorker(QueueEntry work_entry) {
    bool add = (work_entry.op == ADD) ? true : false;
    IFMapClient *client = work_entry.client;

    bool done = ProcessClientWork(add, client);

    return done;
}

void IFMapServer::NodeResetClient(DBGraphVertex *vertex, const BitSet &bset) {
    IFMapNode *node = static_cast<IFMapNode *>(vertex);
    IFMapNodeState *state = exporter_->NodeStateLookup(node);
    if (state) {
        state->InterestReset(bset);
        state->AdvertisedReset(bset);
    }
}

void IFMapServer::LinkResetClient(DBGraphEdge *edge, const BitSet &bset) {
    IFMapLink *link = static_cast<IFMapLink *>(edge);
    IFMapLinkState *state = exporter_->LinkStateLookup(link);
    if (state) {
        state->InterestReset(bset);
        state->AdvertisedReset(bset);
    }
}

// Get the list of subscribed VMs. For each item in the list, if it exist in the
// list of pending vm registration requests, remove it.
void IFMapServer::CleanupUuidMapper(IFMapClient *client) {
    std::vector<std::string> vmlist = client->vm_list();
    for (size_t count = 0; count < vmlist.size(); ++count) {
        vm_uuid_mapper_->CleanupPendingVmRegEntry(vmlist.at(count));
    }
}

void IFMapServer::RemoveSelfAddedLinksAndObjects(IFMapClient *client) {
    IFMapServerTable *vr_table = static_cast<IFMapServerTable *>(
        db_->FindTable("__ifmap__.virtual_router.0"));
    assert(vr_table != NULL);

    IFMapNode *node = vr_table->FindNode(client->identifier());
    if ((node != NULL) && node->IsVertexValid()) {
        IFMapOrigin origin(IFMapOrigin::XMPP);
        for (DBGraphVertex::adjacency_iterator iter = node->begin(graph_), next;
            iter != node->end(graph_); iter = next) {
            IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
            next = ++iter;
            if (adj->table()->name() == "__ifmap__.virtual_machine.0") {
                vr_table->IFMapRemoveVrVmLink(node, adj);
                IFMapServerTable::RemoveObjectAndDeleteNode(adj, origin);
            }
        }
        IFMapServerTable::RemoveObjectAndDeleteNode(node, origin);
    }
}

void IFMapServer::ClientGraphDownload(IFMapClient *client) {
    IFMapTable *table = IFMapTable::FindTable(db_, "virtual-router");
    assert(table);

    IFMapNode *node = table->FindNode(client->identifier());
    if ((node != NULL) && node->IsVertexValid()) {
        for (DBGraphVertex::adjacency_iterator iter = node->begin(graph_);
            iter != node->end(graph_); ++iter) {
            IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
            if (exporter_->FilterNeighbor(node, adj)) {
                continue;
            }
            DBGraphEdge *edge = graph_->GetEdge(node, adj);
            DBTable *link_table = static_cast<DBTable *>(
                db_->FindTable("__ifmap_metadata__.0"));
            link_table->Change(edge);
        }
    }
}

void IFMapServer::ClientGraphCleanup(IFMapClient *client) {
    IFMapTable *table = IFMapTable::FindTable(db_, "virtual-router");
    assert(table);

    IFMapNode *node = table->FindNode(client->identifier());
    if (node != NULL) {
        IFMapNodeState *state = exporter_->NodeStateLookup(node);
        if (state == NULL) {
            return;
        }

        BitSet rm_bs;
        rm_bs.set(client->index());
        state->InterestReset(rm_bs);
        state->AdvertisedReset(rm_bs);

        if (node->IsVertexValid()) {
            graph_->Visit(node,
                boost::bind(&IFMapServer::NodeResetClient, this, _1, rm_bs),
                boost::bind(&IFMapServer::LinkResetClient, this, _1, rm_bs));
        }
    }
}

IFMapClient *IFMapServer::FindClient(const std::string &id) {
    ClientMap::iterator loc = client_map_.find(id);
    if (loc != client_map_.end()) {
        return loc->second;
    }
    return NULL;
}

IFMapClient *IFMapServer::GetClient(int index) {
    IndexMap::iterator loc = index_map_.find(index);
    if (loc != index_map_.end()) {
        return loc->second;
    }
    return NULL;
}

bool IFMapServer::StaleNodesProcTimeout() {
    IFMapStaleCleaner *cleaner = new IFMapStaleCleaner(db_, graph_, this);

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(cleaner);
    return false;
}

void IFMapServer::StaleNodesCleanup() {
    if (stale_cleanup_timer_->running()) {
        stale_cleanup_timer_->Cancel();
    }
    stale_cleanup_timer_->Start(kStaleCleanupTimeout,
            boost::bind(&IFMapServer::StaleNodesProcTimeout, this),
            NULL);
}

void IFMapServer::ProcessVmSubscribe(std::string vr_name, std::string vm_uuid,
                                     bool subscribe, bool has_vms) {
    IFMapVmSubscribe *vm_sub = new IFMapVmSubscribe(db_, graph_, this,
                                                    vr_name, vm_uuid,
                                                    subscribe, has_vms);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(vm_sub);
}

void IFMapServer::ProcessVmSubscribe(std::string vr_name, std::string vm_uuid,
                                     bool subscribe) {
    IFMapClient *client = FindClient(vr_name);
    assert(client);
    IFMapVmSubscribe *vm_sub =
        new IFMapVmSubscribe(db_, graph_, this, vr_name, vm_uuid, subscribe,
                             client->HasVms());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(vm_sub);
}

void IFMapServer::ProcessVmRegAsPending(std::string vm_uuid,
                                        std::string vr_name, bool subscribe) {
    vm_uuid_mapper_->ProcessVmRegAsPending(vm_uuid, vr_name, subscribe);
}

IFMapNode *IFMapServer::GetVmNodeByUuid(const std::string &vm_uuid) {
    return vm_uuid_mapper_->GetVmNodeByUuid(vm_uuid);
}

void IFMapServer::FillClientMap(IFMapServerShowClientMap *out_map) {
    out_map->set_count(client_map_.size());
    out_map->clients.reserve(client_map_.size());
    for (ClientMap::const_iterator iter = client_map_.begin();
         iter != client_map_.end(); ++iter) {
        IFMapClient *client = iter->second;
        IFMapServerClientMapShowEntry entry;
        entry.set_client_name(client->identifier());
        out_map->clients.push_back(entry);
    }
}

void IFMapServer::FillIndexMap(IFMapServerShowIndexMap *out_map) {
    out_map->set_count(index_map_.size());
    out_map->clients.reserve(index_map_.size());
    for (IndexMap::const_iterator iter = index_map_.begin();
         iter != index_map_.end(); ++iter) {
        IFMapClient *client = iter->second;
        IFMapServerIndexMapShowEntry entry;
        entry.set_client_index(iter->first);
        entry.set_client_name(client->identifier());
        out_map->clients.push_back(entry);
    }
}

void IFMapServer::GetUIInfo(IFMapServerInfoUI *server_info) {
    server_info->set_num_peer_clients(GetClientMapSize());
}
