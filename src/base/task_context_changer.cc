#include "base/util.h"
#include "task_context_changer.h"

TaskContextChanger::TaskContextChanger(int taskid, int task_instance) :
    work_queue_(taskid, task_instance,
                boost::bind(&TaskContextChanger::Process, this, _1)) {
}

TaskContextChanger::~TaskContextChanger() {
    work_queue_.Shutdown();
}

bool TaskContextChanger::Process(TaskContextChanger::Data::Type data) {
    data->callback_(data->data_);
    return true;
}

void TaskContextChanger::Enqueue(TaskContextChanger::Callback cb,
                                 TaskContextChanger::ClientData::Type client_data) {
    TaskContextChanger::Data::Type data(new TaskContextChanger::Data());
    data->callback_ = cb;
    data->data_ = client_data;
    work_queue_.Enqueue(data);
}
