/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef event_notifier_h
#define event_notifier_h

#include <base/util.h>
#include <base/queue_task.h>
#include <boost/shared_ptr.hpp>

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
class EventNotifier;

struct EventNotifyKey {
public:
    enum Type {
        END_OF_RIB,
        GENERIC
    };
    EventNotifyKey(Type type);
    virtual ~EventNotifyKey();

    virtual bool IsLess(const EventNotifyKey &rhs) const;
    Type type() const {return type_;}

private:
    tbb::atomic<int> ref_count_;
    Type type_;
};

struct EventNotifyHandle {
    typedef boost::shared_ptr<EventNotifyKey> KeyPtr;
    typedef boost::shared_ptr<EventNotifyHandle> Ptr;
    typedef boost::function<bool(void)> Callback;

    EventNotifyHandle(KeyPtr key, Callback cb);
    virtual ~EventNotifyHandle();
    KeyPtr key() const {return key_;}
    void Notify() const {cb_();}

private:
    KeyPtr key_;
    Callback cb_;
};

class EventNotifier {
public:
    //Callback
    typedef boost::function<bool(void)> Callback;
    //Key Ptr
    typedef boost::shared_ptr<EventNotifyKey> KeyPtr;
    typedef SmartPointerComparator<EventNotifyKey, boost::shared_ptr> Comparator;
    //subscriber callback list, vector of handles
    typedef std::vector<EventNotifyHandle::Ptr> SubscribersList;
    typedef SubscribersList::iterator SubscribersListIter;
    //key to call back list map
    typedef std::map<KeyPtr, SubscribersList, Comparator> NotifyMap;
    typedef NotifyMap::iterator NotifyMapIter;

    EventNotifier(Agent *agent);
    virtual ~EventNotifier();

    //Publisher routines
    void Notify(EventNotifyKey *key);

    //Subscriber routines
    EventNotifyHandle::Ptr RegisterSubscriber(EventNotifyKey *key,
                                              Callback callback);
    void DeregisterSubscriber(EventNotifyHandle::Ptr ptr);

private:
    struct WorkQueueMessage {
    public:
        typedef boost::shared_ptr<WorkQueueMessage> Ptr;
        enum Type {
            PUBLISHER,
            REGISTER_SUBSCRIBER,
            DEREGISTER_SUBSCRIBER
        };
        WorkQueueMessage(Type type, EventNotifyHandle::Ptr handle);
        Type type_;
        EventNotifyHandle::Ptr handle_ptr_;
    };
    //Work queue enqueue/dequeue
    bool Enqueue(WorkQueueMessage::Ptr data);
    bool Process(WorkQueueMessage::Ptr data);

    void NotifyInternal(KeyPtr key);
    void RegisterSubscriberInternal(EventNotifyHandle::Ptr ptr);
    void DeRegisterSubscriberInternal(EventNotifyHandle::Ptr ptr);

    NotifyMap map_;
    WorkQueue<WorkQueueMessage::Ptr> work_queue_;
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(EventNotifier);
};

#endif //event_notifier_h
