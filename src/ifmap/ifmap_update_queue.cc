/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_update_queue.h"

#include <boost/checked_delete.hpp>
#include <boost/assign/list_of.hpp>
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_server.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>
#include "ifmap/ifmap_server_show_types.h"
#include "bgp/bgp_sandesh.h"

IFMapUpdateQueue::IFMapUpdateQueue(IFMapServer *server) : server_(server) {
    list_.push_back(tail_marker_);
}

struct IFMapListEntryDisposer {
    void operator()(IFMapListEntry *ptr) {
        boost::checked_delete(ptr);
    }
};

IFMapUpdateQueue::~IFMapUpdateQueue() {
    list_.erase(list_.iterator_to(tail_marker_));
    list_.clear_and_dispose(IFMapListEntryDisposer());
}

bool IFMapUpdateQueue::Enqueue(IFMapUpdate *update) {
    assert(!update->advertise().empty());
    bool tm_last = false;
    if (GetLast() == tail_marker()) {
        tm_last = true;
    }
    list_.push_back(*update);
    return tm_last;
}

void IFMapUpdateQueue::Dequeue(IFMapUpdate *update) {
    list_.erase(list_.iterator_to(*update));
}

IFMapMarker *IFMapUpdateQueue::GetMarker(int bit) {
    MarkerMap::iterator loc = marker_map_.find(bit);
    if (loc == marker_map_.end()) {
        return NULL;
    }
    return loc->second;
}

void IFMapUpdateQueue::Join(int bit) {
    IFMapMarker *marker = &tail_marker_;
    marker->mask.set(bit);
    marker_map_.insert(std::make_pair(bit, marker));
}

void IFMapUpdateQueue::Leave(int bit) {
    MarkerMap::iterator loc = marker_map_.find(bit);
    assert(loc != marker_map_.end());
    IFMapMarker *marker = loc->second;

    BitSet reset_bs;
    reset_bs.set(bit);

    // Start with the first element after the client's marker
    for (List::iterator iter = list_.iterator_to(*marker), next;
         iter != list_.end(); iter = next) {
        IFMapListEntry *item = iter.operator->();
        next = ++iter;
        if (item->IsMarker()) {
            continue;
        }
        IFMapUpdate *update = static_cast<IFMapUpdate *>(item);
        update->AdvertiseReset(reset_bs);
        if (update->advertise().empty()) {
            Dequeue(update);
        }

        // Update may be freed.
        server_->exporter()->StateUpdateOnDequeue(update, reset_bs, true);
    }

    marker_map_.erase(loc);
    marker->mask.reset(bit);
    if ((marker != &tail_marker_)  && (marker->mask.empty())) {
        list_.erase(list_.iterator_to(*marker));
        delete marker;
    }
}

void IFMapUpdateQueue::MarkerMerge(IFMapMarker *dst, IFMapMarker *src,
                                   const BitSet &mmove) {
    //
    // Set the bits in dst and update the MarkerMap.  Be sure to set the dst
    // before we reset the src since bitset maybe a reference to src->mask.
    // Call to operator|=()
    //
    dst->mask |= mmove;
    for (size_t i = mmove.find_first();
         i != BitSet::npos; i = mmove.find_next(i)) {
        MarkerMap::iterator loc = marker_map_.find(i);
        assert(loc != marker_map_.end());
        loc->second = dst;
    }
    // Reset the bits in the src and get rid of it in case it's now empty.
    src->mask.Reset(mmove);
    if (src->mask.empty()) {
        assert(src != &tail_marker_);
        list_.erase(list_.iterator_to(*src));
        delete src;
    }
}

IFMapMarker* IFMapUpdateQueue::MarkerSplit(IFMapMarker *marker,
                                           IFMapListEntry *current, 
                                           const BitSet &msplit, bool before) {
    assert(!msplit.empty());
    IFMapMarker *new_marker = new IFMapMarker();

    // call to operator=()
    new_marker->mask = msplit;
    marker->mask.Reset(msplit);
    assert(!marker->mask.empty());

    for (size_t i = msplit.find_first();
         i != BitSet::npos; i = msplit.find_next(i)) {
        MarkerMap::iterator loc = marker_map_.find(i);
        assert(loc != marker_map_.end());
        loc->second = new_marker;
    }
    if (before) {
        // Insert new_marker before current
        list_.insert(list_.iterator_to(*current), *new_marker);
    } else {
        // Insert new_marker after current
        list_.insert(++list_.iterator_to(*current), *new_marker);
    }
    return new_marker;
}

IFMapMarker* IFMapUpdateQueue::MarkerSplitBefore(IFMapMarker *marker,
                                                 IFMapListEntry *current, 
                                                 const BitSet &msplit) {
    bool before = true;
    IFMapMarker *ret_marker = MarkerSplit(marker, current, msplit, before);
    return ret_marker;
}

IFMapMarker* IFMapUpdateQueue::MarkerSplitAfter(IFMapMarker *marker,
                                                IFMapListEntry *current, 
                                                const BitSet &msplit) {
    bool before = false;
    IFMapMarker *ret_marker = MarkerSplit(marker, current, msplit, before);
    return ret_marker;
}

// Insert marker before current
void IFMapUpdateQueue::MoveMarkerBefore(IFMapMarker *marker,
                                        IFMapListEntry *current) {
    if (marker != current) {
        list_.erase(list_.iterator_to(*marker));
        list_.insert(list_.iterator_to(*current), *marker);
    }
}

// Insert marker after current
void IFMapUpdateQueue::MoveMarkerAfter(IFMapMarker *marker,
                                       IFMapListEntry *current) {
    if (marker != current) {
        list_.erase(list_.iterator_to(*marker));
        list_.insert(++list_.iterator_to(*current), *marker);
    }
}

IFMapListEntry *IFMapUpdateQueue::Previous(IFMapListEntry *current) {
    List::iterator iter = list_.iterator_to(*current);
    if (iter == list_.begin()) {
        return NULL;
    }
    --iter;
    return iter.operator->();
}

IFMapListEntry *IFMapUpdateQueue::GetLast() {
    // the list must always have the tail_marker
    assert(!list_.empty());
    List::reverse_iterator riter;
    riter = list_.rbegin();
    return riter.operator->();
}

IFMapListEntry * IFMapUpdateQueue::Next(IFMapListEntry *current) {
    List::iterator iter = list_.iterator_to(*current);
    if (++iter == list_.end()) {
        return NULL;
    }
    return iter.operator->();
}

bool IFMapUpdateQueue::empty() const {
    return (list_.begin().operator->() == &tail_marker_) &&
            (list_.rbegin().operator->() == &tail_marker_);
}

int IFMapUpdateQueue::size() const {
    return (int)list_.size();
}

void IFMapUpdateQueue::PrintQueue() {
    int i = 0;
    IFMapListEntry *item;
    List::iterator iter = list_.iterator_to(list_.front());
    while (iter != list_.end()) {
        item = iter.operator->();
        if (item->IsMarker()) {
            IFMapMarker *marker = static_cast<IFMapMarker *>(item);
            if (marker == &tail_marker_) {
                std::cout << i << ". Tail Marker: " << item;
            } else {
                std::cout << i << ". Marker: " << item;
            }
            std::cout << " clients:";
            for (size_t j = marker->mask.find_first();
                j != BitSet::npos; j = marker->mask.find_next(j)) {
                std::cout << " " << j;
            }
            std::cout << std::endl;
        }
        if (item->IsUpdate()) {
            std::cout << i << ". Update: " << item << " ";
        }
        if (item->IsDelete()) {
            std::cout << i << ". Delete: " << item << " ";
        }
        if (item->IsUpdate() || item->IsDelete()) {
            IFMapUpdate *update = static_cast<IFMapUpdate *>(item);
            const IFMapObjectPtr ref = update->data();
            if (ref.type == IFMapObjectPtr::NODE) {
                std::cout << "node <";
                std::cout << ref.u.node->name() << ">" << std::endl;
            } else if (ref.type == IFMapObjectPtr::LINK) {
                std::cout << ref.u.link->ToString() << std::endl;
            }
        }

        iter++;
        i++;
    }
    std::cout << "**End of queue**" << std::endl;
}

// almost everything in this class is static since we dont really want to
// intantiate this class
class ShowIFMapUpdateQueue {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        std::vector<UpdateQueueShowEntry> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there is
        // no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        std::vector<UpdateQueueShowEntry>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static void CopyNode(UpdateQueueShowEntry *dest, IFMapListEntry *src,
                         IFMapUpdateQueue *queue);
    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
};

void ShowIFMapUpdateQueue::CopyNode(UpdateQueueShowEntry *dest,
                                    IFMapListEntry *src,
                                    IFMapUpdateQueue *queue) {
    if (src->IsUpdate() || src->IsDelete()) {
        IFMapUpdate *update = static_cast<IFMapUpdate *>(src);
        const IFMapObjectPtr ref = update->data();
        if (ref.type == IFMapObjectPtr::NODE) {
            dest->node_name = "<![CDATA[" + ref.u.node->name() + "]]>";
        } else if (ref.type == IFMapObjectPtr::LINK) {
            dest->node_name = "<![CDATA[" + ref.u.link->ToString() + "]]>";
        }
        if (src->IsUpdate()) {
            dest->qe_type = "Update";
        }
        if (src->IsDelete()) {
            dest->qe_type = "Delete";
        }
        dest->qe_bitset = update->advertise().ToString();
    }
    if (src->IsMarker()) {
        IFMapMarker *marker = static_cast<IFMapMarker *>(src);
        dest->node_name = "Marker";
        if (marker == queue->tail_marker()) {
            dest->qe_type = "Tail-Marker";
        } else {
            dest->qe_type = "Marker";
        }
        dest->qe_bitset = marker->mask.ToString();
    }
}

bool ShowIFMapUpdateQueue::BufferStage(const Sandesh *sr,
                                       const RequestPipeline::PipeSpec ps,
                                       int stage, int instNum,
                                       RequestPipeline::InstData *data) {
    const IFMapUpdateQueueShowReq *request = 
        static_cast<const IFMapUpdateQueueShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());
    ShowData *show_data = static_cast<ShowData *>(data);

    IFMapUpdateQueue *queue = bsc->ifmap_server->queue();
    assert(queue);
    show_data->send_buffer.reserve(queue->list_.size());

    IFMapUpdateQueue::List::iterator iter = 
        queue->list_.iterator_to(queue->list_.front());
    while (iter != queue->list_.end()) {
        IFMapListEntry *item = iter.operator->();

        UpdateQueueShowEntry dest;
        CopyNode(&dest, item, queue);
        show_data->send_buffer.push_back(dest);

        iter++;
    }

    return true;
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapUpdateQueue::SendStage(const Sandesh *sr,
                                     const RequestPipeline::PipeSpec ps,
                                     int stage, int instNum,
                                     RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapUpdateQueue::ShowData &show_data = 
        static_cast<const ShowIFMapUpdateQueue::ShowData &>
                                                (prev_stage_data->at(0));
    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    std::vector<UpdateQueueShowEntry> dest_buffer;
    std::vector<UpdateQueueShowEntry>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapUpdateQueueShowReq *request = 
        static_cast<const IFMapUpdateQueueShowReq *>(ps.snhRequest_.get());
    IFMapUpdateQueueShowResp *response = new IFMapUpdateQueueShowResp();
    response->set_queue(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapUpdateQueueShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapUpdateQueue::AllocBuffer;
    s0.cbFn_ = ShowIFMapUpdateQueue::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapUpdateQueue::AllocTracker;
    s1.cbFn_ = ShowIFMapUpdateQueue::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= boost::assign::list_of(s0)(s1);
    RequestPipeline rp(ps);
}
