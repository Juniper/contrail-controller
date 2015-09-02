/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"

#include <boost/assign.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>

#include "base/task.h"
#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "testing/gunit.h"

using namespace boost::assign;
using namespace std;

class ExampleType {
  public:
    ExampleType() : count_(0) { }
    void Produce() {
        CHECK_CONCURRENCY("test::producer", "test::exclusive");
        count_++;
    }

    void Consume() {
        CHECK_CONCURRENCY("test::consumer");
        count_--;
    }

    int count() const { return count_; }

  private:
    int count_;
};

class Executer : public Task {
  public:
    typedef boost::function<void(void)> Function;
    Executer(int task_id, Function func) : Task(task_id), func_(func) { }
    bool Run() {
        (func_)();
        return true;
    }
    static void Execute(const string &task_id, Function func) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        Executer *executer = new Executer(scheduler->GetTaskId(task_id), func);
        scheduler->Enqueue(executer);
    }

  private:
    Function func_;
};

class TaskAnnotationsTest : public ::testing::Test {
  protected:
    TaskAnnotationsTest() { }
    void Produce(const string &task_id) {
        Executer::Execute(task_id, boost::bind(&ExampleType::Produce, &var_));
    }
    void Consume(const string &task_id) {
        Executer::Execute(task_id, boost::bind(&ExampleType::Consume, &var_));
    }
    ExampleType var_;
};

TEST_F(TaskAnnotationsTest, Correct) {
    Produce("test::producer");
    Consume("test::consumer");
    Produce("test::exclusive");
    Consume("test::consumer");
    task_util::WaitForIdle();
    EXPECT_EQ(0, var_.count());
}

typedef TaskAnnotationsTest TaskAnnotationsDeathTest;

TEST_F(TaskAnnotationsDeathTest, Failure) {
    ASSERT_DEATH({
    Produce("test::consumer");
    task_util::WaitForIdle();
    EXPECT_EQ(1, var_.count());
        }, "");
}

class TestEnvironment : public ::testing::Environment {
  public:
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        TaskPolicy po_exclusive =
                list_of(TaskExclusion(scheduler->GetTaskId("test::producer")))
                (TaskExclusion(scheduler->GetTaskId("test::consumer")));
        scheduler->SetPolicy(scheduler->GetTaskId("test::exclusive"),
                             po_exclusive);
        TaskPolicy po_consumer =
                list_of(TaskExclusion(scheduler->GetTaskId("test::producer")));
        scheduler->SetPolicy(scheduler->GetTaskId("test::consumer"),
                             po_consumer);
    }

    virtual void TearDown() {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Terminate();
    }
};

int main(int argc, char *argv[]) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    return RUN_ALL_TESTS();
}
