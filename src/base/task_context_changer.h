/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __TASK_CONTEXT_CHANGER__
#define __TASK_CONTEXT_CHANGER__

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include "base/queue_task.h"
#include "base/task.h"

class TaskContextChanger {
public:
   TaskContextChanger(int taskid, int task_instance);
   virtual ~TaskContextChanger();

   struct ClientData {
       typedef boost::shared_ptr<TaskContextChanger::ClientData> Type;
   };
   typedef boost::function<void(TaskContextChanger::ClientData::Type)> Callback;
   struct Data {
       typedef boost::shared_ptr<TaskContextChanger::Data> Type;
       TaskContextChanger::Callback callback_;
       TaskContextChanger::ClientData::Type data_;
   };

   void Enqueue(TaskContextChanger::Callback cb,
                TaskContextChanger::ClientData::Type data);
   bool Process(TaskContextChanger::Data::Type data);

private:    
   WorkQueue<TaskContextChanger::Data::Type> work_queue_;
   DISALLOW_COPY_AND_ASSIGN(TaskContextChanger);
};

#endif
