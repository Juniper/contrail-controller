/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <cmn/event_notifier.h>

//Event registeration handle
EventNotifyHandle::EventNotifyHandle(EventNotifyHandle::KeyPtr key,
                   EventNotifyHandle::TaskTriggerPtr task_trigger_ptr) :
    key_(key), task_trigger_ptr_(task_trigger_ptr) {
}

EventNotifyHandle::~EventNotifyHandle() {
}

//Event notify manager
EventNotifyManager::EventNotifyManager() : map_() {
}

EventNotifyManager::~EventNotifyManager() {
    assert(map_.empty() == true);
}

void EventNotifyManager::Notify(KeyPtr key) {
    EventNotifyManager::NotifyMapIter map_it = map_.find(key);
    if (map_it == map_.end()) {
        //No subscribers
        return;
    }
    EventNotifyManager::SubscriberCbList list = map_it->second;
    for (EventNotifyManager::SubscriberCbListIter it = list.begin();
         it != list.end(); it++) {
        (*it)->Set();
    }
}

EventNotifyHandle::Ptr
EventNotifyManager::RegisterSubscriber(EventNotifyKey *key,
                                       EventNotifyManager::Cb cb,
                                       int task_id) {
    KeyPtr key_ptr = static_cast<KeyPtr>(key);
    EventNotifyManager::TaskTriggerPtr ptr(new TaskTrigger(cb, task_id, 0));
    //It is not ensured that same subscriber is not registered multiple times.
    EventNotifyManager::NotifyMapIter map_it = map_.find(key_ptr);
    if (map_it == map_.end()) {
        map_[key_ptr].push_back(ptr);
    } else {
        (*map_it).second.push_back(ptr);
    }

    return (EventNotifyHandle::Ptr(new EventNotifyHandle(key, ptr)));
}

void EventNotifyManager::DeregisterSubscriber(EventNotifyHandle::Ptr handle) {
    if (handle == NULL)
        return;

    KeyPtr key_ptr = static_cast<KeyPtr>(handle->key());
    EventNotifyManager::NotifyMapIter map_it = map_.find(key_ptr);
    if (map_it == map_.end()) {
        return;
    }

    EventNotifyManager::SubscriberCbListIter it =
        std::find(map_it->second.begin(), map_it->second.end(),
                  handle->task_trigger_ptr());
    if (it == map_it->second.end()) {
        return;
    }
    map_it->second.erase(it);

    if (map_it->second.empty()) {
        map_.erase(map_it);
    }
    return;
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

inline void intrusive_ptr_add_ref(EventNotifyKey* ptr) {
    ptr->ref_count_.fetch_and_increment();
}

inline void intrusive_ptr_release(EventNotifyKey* ptr) {
    uint32_t prev = ptr->ref_count_.fetch_and_decrement();
    if (prev == 1) {
      delete ptr;
    }
}
