/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DB_IFMAP_EXPORTER_H__
#define __DB_IFMAP_EXPORTER_H__

#include <list>
#include <memory>
#include <map>
#include <string>
#include <boost/crc.hpp>      // for boost::crc_32_type
#include <boost/scoped_ptr.hpp>
#include <boost/unordered_set.hpp>

#include "db/db_table.h"

class BitSet;

class IFMapClient;
class IFMapGraphWalker;
class IFMapLink;
class IFMapLinkState;
class IFMapNode;
class IFMapNodeState;
class IFMapServer;
class IFMapState;
class IFMapTable;
class IFMapUpdate;
class IFMapUpdateQueue;
class IFMapUpdateSender;

struct IFMapTypenameWhiteList;

// The IFMapExporter makes sure that the right entries are added to the update
// queue. It uses the GraphWalker to calculate the 'interest' set for each node
// and then ensures that all clients see the necessary information. It also
// enforces ordering. All link add/change operations must come after the nodes
// they refer to have been advertised to the client. Add node delete operations
// must come after the links that refer to the node have been deleted.
class IFMapExporter {
public:
    enum TrackerType {
        INTEREST,
        ADVERTISED,
        TT_END,
    };
    typedef boost::unordered_set<IFMapState *> ConfigSet;
    typedef ConfigSet::size_type CsSz_t;
    typedef ConfigSet::const_iterator Cs_citer;
    typedef std::vector<ConfigSet *> ClientConfigTracker;
    typedef boost::crc_32_type::value_type crc32type;
    explicit IFMapExporter(IFMapServer *server);
    ~IFMapExporter();

    void Initialize(DB *db);
    void Shutdown();

    void StateUpdateOnDequeue(IFMapUpdate *update, const BitSet &dequeue_set,
                              bool is_delete);

    // GraphWalker API
    DBTable::ListenerId TableListenerId(const DBTable *table) const;

    IFMapNodeState *NodeStateLocate(IFMapNode *node);
    IFMapNodeState *NodeStateLookup(IFMapNode *node);
    IFMapLinkState *LinkStateLookup(IFMapLink *link);

    DBTable *link_table() { return link_table_; }
    IFMapServer *server() { return server_; }

    bool FilterNeighbor(IFMapNode *lnode, IFMapNode *rnode);

    void AddClientConfigTracker(int index);
    void DeleteClientConfigTracker(int index);
    void UpdateClientConfigTracker(IFMapState *state, const BitSet& client_bits,
                                   bool add, TrackerType tracker_type);
    void CleanupClientConfigTrackedEntries(int index);
    bool ClientHasConfigTracker(TrackerType tracker_type, int index);
    bool ClientConfigTrackerHasState(TrackerType tracker_type, int index,
                                     IFMapState *state);
    bool ClientConfigTrackerEmpty(TrackerType tracker_type, int index);
    size_t ClientConfigTrackerSize(TrackerType tracker_type, int index);
    Cs_citer ClientConfigTrackerBegin(TrackerType tracker_type, int index) const;
    Cs_citer ClientConfigTrackerEnd(TrackerType tracker_type, int index) const;

    void StateInterestSet(IFMapState *state, const BitSet& interest_bits);
    void StateInterestOr(IFMapState *state, const BitSet& interest_bits);
    void StateInterestReset(IFMapState *state, const BitSet& interest_bits);
    void StateAdvertisedOr(IFMapState *state, const BitSet& interest_bits);
    void StateAdvertisedReset(IFMapState *state, const BitSet& interest_bits);

    const IFMapTypenameWhiteList &get_traversal_white_list() const;
    void ResetLinkDeleteClients(const BitSet &bset);

private:
    friend class XmppIfmapTest;
    class TableInfo;
    typedef std::map<DBTable *, TableInfo *> TableMap;

    // Database listener for IFMap identifier (and link attr) tables.
    void NodeTableExport(DBTablePartBase *partition, DBEntryBase *entry);
    // Database listener for the IFMapLink DB Table.
    void LinkTableExport(DBTablePartBase *partition, DBEntryBase *entry);

    template <class ObjectType>
    bool UpdateAddChange(ObjectType *obj, IFMapState *state,
                         const BitSet &add_set, const BitSet &rm_set,
                         bool change);
    template <class ObjectType>
    bool UpdateRemove(ObjectType *obj, IFMapState *state,
                      const BitSet &rm_set);
    template <class ObjectType>
    void EnqueueDelete(ObjectType *obj, IFMapState *state);

    void MoveDependentLinks(IFMapNodeState *state);
    void RemoveDependentLinks(IFMapNodeState *state, const BitSet &rm_set);
    void MoveAdjacentNode(IFMapNodeState *state);
    void ProcessAdjacentNode(IFMapNode *node, const BitSet &add_set,
                             IFMapNodeState *state);

    bool IsFeasible(const IFMapNode *node);

    const BitSet *MergeClientInterest(IFMapNode *node, IFMapNodeState *state,
                                      std::auto_ptr<BitSet> *ptr);

    const TableInfo *Find(const DBTable *table) const;

    void TableStateClear(DBTable *table, DBTable::ListenerId tsid);
    bool ConfigChanged(IFMapNode *node);
    void DeleteStateIfAppropriate(DBTable *table, DBEntryBase *entry,
                                  IFMapState *state);

    IFMapUpdateQueue *queue();
    IFMapUpdateSender *sender();

    IFMapServer *server_;
    boost::scoped_ptr<IFMapGraphWalker> walker_;
    TableMap table_map_;

    DBTable *link_table_;
    ClientConfigTracker client_config_tracker_[TT_END];
};

#endif
