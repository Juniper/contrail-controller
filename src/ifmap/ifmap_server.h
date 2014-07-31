/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_server__
#define __ctrlplane__ifmap_server__

#include <map>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/bitset.h"
#include "base/timer.h"
#include "net/address.h"
#include "base/queue_task.h"
#include "ifmap/client/ifmap_manager.h"

class DB;
class DBGraph;
class DBGraphEdge;
class DBGraphVertex;
class IFMapChannelManager;
class IFMapClient;
class IFMapExporter;
class IFMapNode;
class IFMapUpdateQueue;
class IFMapUpdateSender;
class IFMapVmUuidMapper;
class IFMapServerShowClientMap;
class IFMapServerShowIndexMap;
class IFMapTableListEntry;
class IFMapNodeTableListShowEntry;
class IFMapServerInfoUI;

class IFMapServer {
public:
    typedef std::map<std::string, IFMapClient *> ClientMap;
    typedef std::map<int, IFMapClient *> IndexMap;
    typedef ClientMap::size_type CmSz_t;
    IFMapServer(DB *db, DBGraph *graph, boost::asio::io_service *io_service);
    virtual ~IFMapServer();

    // Must be called after the __ifmap__ tables are registered with the
    // database.
    void Initialize();
    void Shutdown();

    void ClientRegister(IFMapClient *client);
    void ClientUnregister(IFMapClient *client);
    bool ProcessClientWork(bool add, IFMapClient *client);

    IFMapClient *FindClient(const std::string &id);
    IFMapClient *GetClient(int index);

    void AddClient(IFMapClient *client);
    void DeleteClient(IFMapClient *client);
    void StaleNodesCleanup();

    DB *database() { return db_; }
    DBGraph *graph() { return graph_; }
    IFMapUpdateQueue *queue() { return queue_.get(); }
    IFMapUpdateSender *sender() { return sender_.get(); }
    IFMapExporter *exporter() { return exporter_.get(); }
    IFMapVmUuidMapper *vm_uuid_mapper() { return vm_uuid_mapper_.get(); }
    boost::asio::io_service *io_service() { return io_service_; }
    void set_ifmap_manager(IFMapManager *manager) {
        ifmap_manager_ = manager;
    }
    IFMapManager *get_ifmap_manager() {
        return ifmap_manager_;
    }
    virtual uint64_t get_ifmap_channel_sequence_number() {
        return ifmap_manager_->GetChannelSequenceNumber();
    }
    void set_ifmap_channel_manager(IFMapChannelManager *manager) {
        ifmap_channel_manager_ = manager;
    }
    IFMapChannelManager *get_ifmap_channel_manager() {
        return ifmap_channel_manager_;
    }

    void ProcessVmSubscribe(std::string vr_name, std::string vm_uuid,
                            bool subscribe, bool has_vms);
    void ProcessVmSubscribe(std::string vr_name, std::string vm_uuid,
                            bool subscribe);

    class IFMapStaleCleaner;
    class IFMapVmSubscribe;

    void ProcessVmRegAsPending(std::string vm_uuid, std::string vr_name,
                               bool subscribe);
    IFMapNode *GetVmNodeByUuid(const std::string &vm_uuid);

    void FillClientMap(IFMapServerShowClientMap *out_map);
    void FillIndexMap(IFMapServerShowIndexMap *out_map);
    const CmSz_t GetClientMapSize() const { return client_map_.size(); }
    void GetUIInfo(IFMapServerInfoUI *server_info);

private:
    static const int kStaleCleanupTimeout = 60000; // milliseconds
    friend class IFMapServerTest;
    friend class IFMapRestartTest;
    friend class ShowIFMapXmppClientInfo;
    friend class XmppIfmapTest;

    enum QueueOp {
        ADD = 1,
        DELETE = 2
    };
    struct QueueEntry {
        QueueOp op;
        IFMapClient *client;
    };
    bool ClientWorker(QueueEntry work_entry);
    void ClientGraphDownload(IFMapClient *client);
    void ClientGraphCleanup(IFMapClient *client);
    void RemoveSelfAddedLinksAndObjects(IFMapClient *client);
    void CleanupUuidMapper(IFMapClient *client);
    void LinkResetClient(DBGraphEdge *edge, const BitSet &bset);
    void NodeResetClient(DBGraphVertex *vertex, const BitSet &bset);
    bool StaleNodesProcTimeout();
    const ClientMap &GetClientMap() const { return client_map_; }
    void SimulateDeleteClient(IFMapClient *client);

    DB *db_;
    DBGraph *graph_;
    boost::scoped_ptr<IFMapUpdateQueue> queue_;
    boost::scoped_ptr<IFMapExporter> exporter_;
    boost::scoped_ptr<IFMapUpdateSender> sender_;
    boost::scoped_ptr<IFMapVmUuidMapper> vm_uuid_mapper_;
    BitSet client_indexes_;
    ClientMap client_map_;
    IndexMap index_map_;
    WorkQueue<QueueEntry> work_queue_;
    boost::asio::io_service *io_service_;
    Timer *stale_cleanup_timer_;
    IFMapManager *ifmap_manager_;
    IFMapChannelManager *ifmap_channel_manager_;
};

#endif /* defined(__ctrlplane__ifmap_server__) */
