/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/lifetime.h"

#include <boost/bind.hpp>

LifetimeRefBase::LifetimeRefBase(LifetimeActor *actor)
        : ref_(this, actor) {
}

LifetimeRefBase::~LifetimeRefBase() {
}

LifetimeActor::LifetimeActor(LifetimeManager *manager)
        : manager_(manager), refcount_(0), shutdown_invoked_(false),
          delete_paused_(false),
          create_time_stamp_usecs_(UTCTimestampUsec()),
          delete_time_stamp_usecs_(0) {
    deleted_ = false;
}

LifetimeActor::~LifetimeActor() {
    assert(refcount_ == 0);
    assert(dependents_.empty());
}

//
// Concurrency: called in the context of any Task or the main thread.
//
// Used to trigger delete for managed object and it's dependents. Enqueue
// this actor to the Lifetime Manager. Propagation of the delete operation
// to dependents happens in the context of the LifetimeManager's Task.
//
void LifetimeActor::Delete() {
    if (deleted_.fetch_and_store(true)) {
        return;
    }
    delete_time_stamp_usecs_ = UTCTimestampUsec();
    manager_->Enqueue(this);
}

//
// Concurrency: called in the context of the LifetimeManager's Task.
//
// Used to propagate delete for a managed object to it's dependents.
//
// Walk the list of dependent LifetimeRefs and propagate the delete. A mutex is
// used to ensure that the dependent list does not change while we are walking
// through it.
//
void LifetimeActor::PropagateDelete() {
    assert(deleted_);
    tbb::mutex::scoped_lock lock(mutex_);
    for (Dependents::iterator iter = dependents_.begin();
         iter != dependents_.end(); ++iter) {
        iter->Delete();
    }
}

//
// Concurrency: called in the context of any Task or the main thread.
//
// Enqueue a delete event for this actor to the LifetimeManager.
//
void LifetimeActor::RetryDelete() {
    assert(deleted_);
    manager_->Enqueue(this);
}

//
// Concurrency: called in the context of the LifetimeManager's Task.
//
// Called immediately before the object is destroyed.
//
void LifetimeActor::DeleteComplete() {
}

//
// Concurrency: called in the context of the LifetimeManager's Task.
//
// Can be called multiple times - when the managed object is initially deleted
// and whenever a delete event is enqueued to the Lifetime Manager. The latter
// happens when the list of dependents becomes empty or the refcount of the
// lightweight dependents goes to 0.
//
void LifetimeActor::Shutdown() {
}

//
// Concurrency: called in the context of main thread.
// TaskScheduler should be stopped prior to invoking this method.
//
// Prevent object from getting destroyed - testing only.
//
void LifetimeActor::PauseDelete() {
    tbb::mutex::scoped_lock lock(mutex_);
    assert(!deleted_);
    delete_paused_ = true;
}

//
// Concurrency: called in the context of main thread.
// TaskScheduler should be stopped prior to invoking this method.
//
// Allow object to get destroyed - testing only.
//
void LifetimeActor::ResumeDelete() {
    tbb::mutex::scoped_lock lock(mutex_);
    assert(deleted_);
    delete_paused_ = false;
    refcount_++;
    manager_->EnqueueNoIncrement(this);
}

//
// Concurrency: called in the context of any Task.
//
// Add the LifetimeRef as a dependent for this Actor.
//
void LifetimeActor::DependencyAdd(
    DependencyRef<LifetimeRefBase, LifetimeActor> *node) {
    tbb::mutex::scoped_lock lock(mutex_);
    assert(!deleted_);
    dependents_.Add(node);
}

//
// Concurrency: called in the context of the LifetimeManager's Task.
//
// Remove the LifetimeRef as a dependent of this Actor.  Note that this can
// happen when the dependent object itself is being deleted i.e. this actor
// itself need not be marked deleted.
//
void LifetimeActor::DependencyRemove(
    DependencyRef<LifetimeRefBase, LifetimeActor> *node) {
    tbb::mutex::scoped_lock lock(mutex_);
    dependents_.Remove(node);
    if (deleted_ && dependents_.empty()) {
        refcount_++;
        manager_->EnqueueNoIncrement(this);
    }
}

// When the actor is placed in the queue the caller must still hold an
// "lock" on the object in the form of either a dependency or an
// explicit test performed by the derived class MayDelete() method.
void LifetimeActor::ReferenceIncrement() {
    tbb::mutex::scoped_lock lock(mutex_);
    refcount_++;
}

bool LifetimeActor::ReferenceDecrementAndTest()  {
    tbb::mutex::scoped_lock lock(mutex_);
    refcount_--;
    return (refcount_ == 0 && dependents_.empty() && !delete_paused_ &&
            MayDelete());
}

LifetimeManager::LifetimeManager(int task_id)
    : defer_count_(0),
      queue_(task_id, 0,
        boost::bind(&LifetimeManager::DeleteExecutor, this, _1)) {
}

LifetimeManager::~LifetimeManager() {
    queue_.Shutdown();
}

//
// Disable/Enable the WorkQueue - testing only.
//
void LifetimeManager::SetQueueDisable(bool disabled) {
    queue_.set_disable(disabled);
}

//
// Concurrency: called in the context of any Task or the main thread.
//
// Enqueue a delete event for the actor.
//
void LifetimeManager::Enqueue(LifetimeActor *actor) {
    LifetimeActorRef actor_ref;
    actor->ReferenceIncrement();
    actor_ref.actor = actor;
    queue_.Enqueue(actor_ref);
}

void LifetimeManager::EnqueueNoIncrement(LifetimeActor *actor) {
    LifetimeActorRef actor_ref;
    actor_ref.actor = actor;
    queue_.Enqueue(actor_ref);
}

//
// Concurrency: called in the context of the LifetimeManager's Task.
//
// If this is the first time that the delete actor is being processed, we
// propagate the delete to it's dependents and ask the managed object to
// shut itself i.e. take care of cleaning up any state not represented as
// an explicit LifetimeRef dependent.
//
// If global conditions for object destruction are not satisfied, enqueue
// the delete actor again and defer processing of the queue.  Do not bump
// up the refcount in this case.
// Else go ahead and destroy the object if all conditions are satisfied.
//
bool LifetimeManager::DeleteExecutor(LifetimeActorRef actor_ref) {
    LifetimeActor *actor = actor_ref.actor;
    assert(actor->IsDeleted());
    if (!actor->shutdown_invoked()) {
        actor->PropagateDelete();
        actor->Shutdown();
        actor->set_shutdown_invoked();
    }
    if (!MayDestroy()) {
        EnqueueNoIncrement(actor);
        defer_count_++;
        return false;
    }
    if (actor->ReferenceDecrementAndTest()) {
        actor->DeleteComplete();
        actor->Destroy();
    }
    return true;
}
