/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_update_queue__
#define __ctrlplane__ifmap_update_queue__

#include <map>
#include "ifmap/ifmap_update.h"

class IFMapServer;

class IFMapUpdateQueue {
public:
    typedef boost::intrusive::member_hook<
        IFMapListEntry,
        boost::intrusive::list_member_hook<>,
        &IFMapListEntry::node
    > MemberHook;
    typedef boost::intrusive::list<IFMapListEntry, MemberHook> List;

    typedef std::map<int, IFMapMarker *> MarkerMap;

    explicit IFMapUpdateQueue(IFMapServer *server);

    ~IFMapUpdateQueue();

    // Called from the Exporter to add a new update at the tail of the queue.
    // Returns true if the tail_marker was the last element in the queue.
    bool Enqueue(IFMapUpdate *update);

    // Called from the Exporter to remove an update (because it is going to
    // be superceeded) or by the Sender when the update advertise mask is
    // empty.
    void Dequeue(IFMapUpdate *update);

    // Called by the Sender to iterate through the update queue. Current can
    // be a marker or the previous update.
    IFMapListEntry *Next(IFMapListEntry *current);

    // Returns the element before current or NULL if current is the first
    // element in the list.
    IFMapListEntry *Previous(IFMapListEntry *current);

    // Returns the last element in the list or NULL if the list is empty.
    IFMapListEntry *GetLast();

    // Moves the marker from its previous position to the position immediately
    // 'before' the current node. Used when the Sender stops sending updates or
    // is blocked.
    void MoveMarkerBefore(IFMapMarker *marker, IFMapListEntry *current);

    // Moves the marker from its previous position to the position immediately
    // 'after' the current node. Used when the Sender stops sending updates or
    // is blocked.
    void MoveMarkerAfter(IFMapMarker *marker, IFMapListEntry *current);

    // Removes a set of members from marker, creates a new marker with this set
    // and inserts it 'before' current. Returns the new marker.
    IFMapMarker* MarkerSplitBefore(IFMapMarker *marker, IFMapListEntry *current,
                                   const BitSet &msplit);

    // Removes a set of members from marker, creates a new marker with this set
    // and inserts it 'after' current. Returns the new marker.
    IFMapMarker *MarkerSplitAfter(IFMapMarker *marker, IFMapListEntry *current,
                                  const BitSet &msplit);

    // Moves the specified 'mmove' set from 'src' to 'dst'. If 'src' becomes
    // empty it is deleted.
    void MarkerMerge(IFMapMarker *dst, IFMapMarker *src, const BitSet &mmove);

    // When a new client is established, adds it to the tail marker in order
    // to receive new updates.
    void Join(int bit);

    // When a client session terminate, removes its bit from all the queue
    // entries (marker or otherwise).
    void Leave(int bit);

    // Retrieves the marker corresponding to the client that has the specified
    // index.
    IFMapMarker *GetMarker(int bit);

    // Returns true if the queue is empty.
    bool empty() const;

    IFMapMarker *tail_marker() { return &tail_marker_; }

    int size() const;

    void PrintQueue();

private:
    friend class ShowIFMapUpdateQueue;
    List list_;
    MarkerMap marker_map_;
    IFMapMarker tail_marker_;
    IFMapServer *server_;

    IFMapMarker* MarkerSplit(IFMapMarker *marker, IFMapListEntry *current, 
                             const BitSet &msplit, bool before);
};

#endif /* defined(__ctrlplane__ifmap_update_queue__) */
