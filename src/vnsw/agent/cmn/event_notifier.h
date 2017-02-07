/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef event_notifier_h
#define event_notifier_h

#include <base/util.h>
#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

/*
 * Event Notifier
 *
 * This routine is a kind of pub-sub mechanism for non db notifications.
 *
 * Subscriber:
 * All subscribers can register with a key. Key identifies the type of
 * notification. Subscriber also provides the task context in which it expects
 * the notification. On registeration it is given a handle which can be used to
 * deregister. Same subscriber can also register multiple callbacks. Each
 * registeration is segregated by handle returned agter registeration.
 *
 * Publisher:
 * Publisher uses key like subscriber for kind of notification. Calls Notify and
 * that call in turn call all subscribers. If nobody has subscribed nothing has
 * to be done.
 *
 * Implementatin details:
 * There is a vector of handles maintained on per key basis. To achieve task
 * context based notification on per susbcriber basis, task trigger is used.
 * On every registeration a tasktrigger object is created with callback and task
 * id. This object along with key is bundled in handle which is returned to
 * subscriber.
 *
 */
class TaskTrigger;

struct EventNotifyKey {
public:
    enum Type {
        END_OF_RIB,
        GENERIC
    };
    EventNotifyKey(Type type);
    virtual ~EventNotifyKey();

    virtual bool IsLess(const EventNotifyKey &rhs) const;
    friend void intrusive_ptr_add_ref(EventNotifyKey* ptr);
    friend void intrusive_ptr_release(EventNotifyKey* ptr);
    Type type() const {return type_;}

private:
    tbb::atomic<int> ref_count_;
    Type type_;
};

struct EventNotifyHandle {
    typedef boost::intrusive_ptr<EventNotifyKey> KeyPtr;
    typedef boost::shared_ptr<TaskTrigger> TaskTriggerPtr;
    typedef boost::shared_ptr<EventNotifyHandle> Ptr;

    EventNotifyHandle(KeyPtr key, TaskTriggerPtr ptr);
    virtual ~EventNotifyHandle();
    KeyPtr key() const {return key_;}
    TaskTriggerPtr task_trigger_ptr() const {return task_trigger_ptr_;}

private:
    KeyPtr key_;
    TaskTriggerPtr task_trigger_ptr_;
};

class EventNotifyManager {
public:
    //Callback
    typedef TaskTrigger::FunctionPtr Cb;
    //Key Ptr
    typedef boost::intrusive_ptr<EventNotifyKey> KeyPtr;
    typedef SmartPointerComparator<EventNotifyKey, boost::intrusive_ptr> Comparator;
    //Task trigger typedef
    //TODO may be all similar taskid can be collapsed to same trigger.
    typedef boost::shared_ptr<TaskTrigger> TaskTriggerPtr;
    //subscriber callback list
    typedef std::vector<TaskTriggerPtr> SubscriberCbList;
    typedef SubscriberCbList::iterator SubscriberCbListIter;
    //key to call back list map
    typedef std::map<KeyPtr, SubscriberCbList, Comparator> NotifyMap;
    typedef NotifyMap::iterator NotifyMapIter;

    EventNotifyManager();
    virtual ~EventNotifyManager();

    //Publisher routines
    void Notify(KeyPtr key);

    //Subscriber routines
    EventNotifyHandle::Ptr RegisterSubscriber(EventNotifyKey *key, Cb callback,
                                              int task_id);
    void DeregisterSubscriber(EventNotifyHandle::Ptr ptr);

private:
    NotifyMap map_;
    DISALLOW_COPY_AND_ASSIGN(EventNotifyManager);
};

#endif //event_notifier_h
