/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <cmn/event_notifier.h>

//Event registeration handle
EventNotifyHandle::EventNotifyHandle(KeyPtr key, Callback cb) :
    key_(key), cb_(cb) {
}

EventNotifyHandle::~EventNotifyHandle() {
}

//Event notify manager
EventNotifier::EventNotifier(Agent *agent) : map_(),
  work_queue_(agent->task_scheduler()->GetTaskId(kEventNotifierTask), 0,
              boost::bind(&EventNotifier::Process, this, _1)),
  agent_(agent) {
      work_queue_.set_name("Event Notify Manager");
}

EventNotifier::~EventNotifier() {
    work_queue_.Shutdown();
    assert(map_.empty() == true);
}

bool EventNotifier::Enqueue(WorkQueueMessage::Ptr data) {
    work_queue_.Enqueue(data);
    return true;
}

bool EventNotifier::Process(WorkQueueMessage::Ptr data) {
    if (data->type_ == WorkQueueMessage::PUBLISHER) {
        NotifyInternal(data->handle_ptr_->key());
    } else if (data->type_ == WorkQueueMessage::REGISTER_SUBSCRIBER) {
        RegisterSubscriberInternal(data->handle_ptr_);
    } else if (data->type_ == WorkQueueMessage::DEREGISTER_SUBSCRIBER) {
        DeRegisterSubscriberInternal(data->handle_ptr_);
    }
    return true;
}

void EventNotifier::Notify(EventNotifyKey *key) {
    EventNotifyHandle::Ptr handle(new EventNotifyHandle(KeyPtr(key), Callback()));
    WorkQueueMessage::Ptr data(new WorkQueueMessage(WorkQueueMessage::PUBLISHER,
                               handle));
    work_queue_.Enqueue(data);
}

void EventNotifier::NotifyInternal(KeyPtr key) {
    EventNotifier::NotifyMapIter map_it = map_.find(key);
    if (map_it == map_.end()) {
        //No subscribers
        return;
    }
    EventNotifier::SubscribersList list = map_it->second;
    for (EventNotifier::SubscribersListIter it = list.begin();
         it != list.end(); it++) {
        (*it)->Notify();
    }
}

EventNotifyHandle::Ptr
EventNotifier::RegisterSubscriber(EventNotifyKey *key,
                                       Callback cb) {
    EventNotifyHandle::Ptr handle(new EventNotifyHandle(KeyPtr(key), cb));
    WorkQueueMessage::Ptr data(new WorkQueueMessage
                               (WorkQueueMessage::REGISTER_SUBSCRIBER, handle));
    work_queue_.Enqueue(data);
    return handle;
}

void
EventNotifier::RegisterSubscriberInternal(EventNotifyHandle::Ptr handle) {
    //It is not ensured that same subscriber is not registered multiple times.
    map_[handle->key()].push_back(handle);
}

void EventNotifier::DeregisterSubscriber(EventNotifyHandle::Ptr handle) {
    WorkQueueMessage::Ptr data(new WorkQueueMessage
                               (WorkQueueMessage::DEREGISTER_SUBSCRIBER, handle));
    work_queue_.Enqueue(data);
}

void
EventNotifier::DeRegisterSubscriberInternal(EventNotifyHandle::Ptr handle) {
    if (handle == NULL)
        return;

    KeyPtr key_ptr = static_cast<KeyPtr>(handle->key());
    EventNotifier::NotifyMapIter map_it = map_.find(key_ptr);
    if (map_it == map_.end()) {
        return;
    }

    EventNotifier::SubscribersListIter it =
        std::find(map_it->second.begin(), map_it->second.end(), handle);
    if (it == map_it->second.end()) {
        return;
    }
    map_it->second.erase(it);

    if (map_it->second.empty()) {
        map_.erase(map_it);
    }

    return;
}

EventNotifier::WorkQueueMessage::WorkQueueMessage(Type type,
                                          EventNotifyHandle::Ptr ptr) :
    type_(type), handle_ptr_(ptr) {
}

//EventNotifyKey
EventNotifyKey::EventNotifyKey(Type type) : type_(type) {
    ref_count_ = 0;
}

EventNotifyKey::~EventNotifyKey() {
}

bool EventNotifyKey::IsLess(const EventNotifyKey &rhs) const {
    if (type() != rhs.type()) {
        return (type() < rhs.type());
    }
    return false;
}
