/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE__LIFETIME_H__
#define __BASE__LIFETIME_H__

#include <tbb/atomic.h>
#include <tbb/mutex.h>

#include "base/dependency.h"
#include "base/queue_task.h"
#include "base/util.h"

class LifetimeActor;
class LifetimeManager;

//
// The Lifetime management framework enables a structured approach to the
// problem of tracking inter-dependencies between objects for the purpose
// of orderly deletion and cleanup.  The dependencies are represented as
// an acyclic graph with LifetimeActors being the nodes and LifetimeRefs
// being the links.  The dependency tracker lets us implement "on delete
// cascade" behavior wherein the deletion of an object triggers deletion
// of all it's dependents, the dependents' dependents and so on.
//
// Each object that requires lifetime management contains an instance of
// a derived class of LifetimeActor embedded in it. The derived class is
// specific to the object type and typically contains a back pointer to
// the object. The convention is to name the derived class as DeleteActor
// and the instance of it as deleter_.
//
// Each managed object (other than the root object) contains one or more
// LifetimeRef instances.  A LifetimeRef instance corresponds to a parent
// object on which the object is dependent. The LifetimeRef instances are
// normally created and linked to the parent objects' LifetimeActor when
// the managed object is constructed.  Each LifetimeRef contains a back
// pointer to the managed object.  The convention is to name the members
// as xxx_delete_ref_, where xxx corresponds to the parent object class.
//
// The deletion of a managed object can be triggered by calling Delete on
// it's actor from any thread. This in turn results in cascaded calls to
// Delete as described above. Further, the LifetimeActor also enqueues a
// a delete event to the LifetimeManager for deferred processing.  This
// event is processed by the LifetimeManager in the desired Task context.
//
// When a managed object is destroyed, all the LifetimeRefs to it's parent
// objects are also destroyed.  This automatically causes the LifetimeRefs
// to get removed from the list of dependents in the parents' DeleteActor.
// This in turn also enqueues a delete event to the parent DeleteActor's
// LifetimeManager if appropriate i.e. if the parent DeleteActor's list of
// dependents has become empty.
//
// The LifetimeManager DeleteExecutor processes delete events for Actors
// in the context of the desired Task.  It makes sure that there are no
// dependents and that all relevant state in the DeleteActor's object has
// been cleaned up i.e. the object may be deleted, before destroying the
// object.
//
// Note that object deletes are triggered from top to bottom while object
// destruction happens in the reverse order.
//
// In addition to having other managed objects as dependents it's common
// to also have other lightweight objects as dependents. These lightweight
// dependents should not be tracked using LifetimeRefs, but can instead be
// tracked using simple reference counts. When the reference count becomes
// 0, a delete event for the actor should be posted to the LifetimeManager.
//

//
// Base class for a reference to a managed lifetime object.
//
class LifetimeRefBase {
public:
    LifetimeRefBase(LifetimeActor *actor);
    virtual ~LifetimeRefBase();

    // called when the lifetime actor delete is called.
    // implements "ON DELETE CASCADE" semantics.
    virtual void Delete() = 0;

    void Reset(LifetimeActor *actor) {
        ref_.reset(actor);
    }

    bool IsSet() const { return ref_.get() != NULL; }

private:
    DependencyRef<LifetimeRefBase, LifetimeActor> ref_;
    DISALLOW_COPY_AND_ASSIGN(LifetimeRefBase);
};

//
// Class template to create LifetimeRefs for specific object types.
//
// Ref is the type of the dependent object.
// The LifetimeActor parameter is the actor for the parent object.
//
template <class Ref>
class LifetimeRef : public LifetimeRefBase {
public:
    LifetimeRef(Ref *ptr, LifetimeActor *actor)
            : LifetimeRefBase(actor), ptr_(ptr) {
    }

    virtual void Delete() {
        ptr_->ManagedDelete();
    }

private:
    Ref *ptr_;
    DISALLOW_COPY_AND_ASSIGN(LifetimeRef);
};

// Member of an object that has managed lifetime.
class LifetimeActor {
public:
    LifetimeActor(LifetimeManager *manager);
    virtual ~LifetimeActor();

    // trigger the deletion of a an object.
    // may be called from any thread.
    virtual void Delete();

    virtual void RetryDelete();

    // called to check dependencies.
    virtual bool MayDelete() const = 0;

    // called under the manager thread in order to remove the object state.
    // may be called multiple times.
    virtual void Shutdown();

    // called immediately before the object is destroyed.
    virtual void DeleteComplete();

    // must be called under a specific thread.
    virtual void Destroy() = 0;

    // Prevent/Resume deletion of object - for testing only.
    void PauseDelete();
    void ResumeDelete();

    bool IsDeleted() const { return deleted_; }

    // Decrement the reference count and test whether the object can be
    // destroyed
    void ReferenceIncrement();
    bool ReferenceDecrementAndTest();

    bool shutdown_invoked() { return shutdown_invoked_; }
    void set_shutdown_invoked() { shutdown_invoked_ = true; }

    const uint64_t create_time_stamp_usecs() const {
        return create_time_stamp_usecs_;
    }
    const uint64_t delete_time_stamp_usecs() const {
        return delete_time_stamp_usecs_;
    }

private:
    typedef DependencyList<LifetimeRefBase, LifetimeActor> Dependents;
    friend class DependencyRef<LifetimeRefBase, LifetimeActor>;

    void DependencyAdd(DependencyRef<LifetimeRefBase, LifetimeActor> *node);
    void DependencyRemove(DependencyRef<LifetimeRefBase, LifetimeActor> *node);
    tbb::mutex mutex_;

    LifetimeManager *manager_;
    tbb::atomic<bool> deleted_;
    int refcount_;
    bool shutdown_invoked_;
    bool delete_paused_;
    uint64_t create_time_stamp_usecs_;
    uint64_t delete_time_stamp_usecs_;
    Dependents dependents_;
    DISALLOW_COPY_AND_ASSIGN(LifetimeActor);
};

//
// Handles deletion in the correct task context.
//
// The pointer to the actor is wrapped inside a LifetimeActorRef to prevent
// the WorkQueue from deleting the actor.
//
class LifetimeManager {
public:
    typedef boost::function<bool ()> TaskEntryCallback;
    LifetimeManager(int task_id, TaskEntryCallback on_entry_cb = 0);
    ~LifetimeManager();

    // Enqueue Delete event.
    void Enqueue(LifetimeActor *actor);

    // Enqueue Delete event. Used by the actor code which has already
    // incremented the reference count.
    void EnqueueNoIncrement(LifetimeActor *actor);


    // Return the number of times work queue task executions were deferred.
    size_t GetQueueDeferCount() { return queue_.on_entry_defer_count(); }

private:
    struct LifetimeActorRef {
        LifetimeActor *actor;
    };

    bool DeleteExecutor(LifetimeActorRef actor_ref);

    WorkQueue<LifetimeActorRef> queue_;
    DISALLOW_COPY_AND_ASSIGN(LifetimeManager);
};

#endif
