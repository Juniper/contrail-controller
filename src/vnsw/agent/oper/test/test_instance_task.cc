#include "oper/instance_task.h"
#include "testing/gunit.h"

#include <boost/filesystem.hpp>
#include <boost/bind.hpp>
#include <sstream>
#include <fstream>

#include <cstdlib>
#include <boost/uuid/random_generator.hpp>

#include "io/event_manager.h"
#include "base/logging.h"
#include "base/os.h"
#include "base/test/task_test_util.h"

using namespace std;
using namespace boost::filesystem;

class InstanceTaskFdTest : public ::testing::Test {
public:
    virtual void SetUp() {
        memset(tmpfilename, 0, sizeof(tmpfilename));
        strcpy(tmpfilename, "task_fd_XXXXXX");
        if(mkstemp(tmpfilename) == -1)
            return;
        std::fstream testfile(tmpfilename);
        testfile << "while true; do sleep 1; done";
        testfile.close();
        std::stringstream cmd_str;
        cmd_str << "/bin/sh" << " " <<tmpfilename;
        task_ = new InstanceTaskExecvp("FdTestTask", cmd_str.str(), 1, &evm);
    }

    virtual void TearDown() {
        delete task_;
        remove(tmpfilename);
    }

    pid_t StartTask() {
        /* create few fds */
        if(pipe(pipe_fds) == -1)
            return -1;
        sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_fd < 0)
            return -1;
        /* run the task now */
        task_->Run();
        task_pid = task_->pid();
        return task_pid;
    }

    int StopTask() {
        task_->Stop();
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        close(sock_fd);
    }

    int GetTaskFds() {
        std::stringstream proc_path;
        int no_of_fds;
        proc_path << "/proc/" <<task_pid<<"/fd/";
        path the_path(proc_path.str());
        no_of_fds = std::count_if(directory_iterator(the_path),
                                  directory_iterator(),
                                  bind( static_cast<bool(*)(const path&)>(is_other),
                                  bind( &directory_entry::path, _1)));
        return no_of_fds;
    }

protected:
    int pipe_fds[2];
    int sock_fd;
    InstanceTaskExecvp *task_;
    pid_t task_pid;
    EventManager evm;
    char tmpfilename[L_tmpnam];
};

TEST_F(InstanceTaskFdTest, TestCloseTaskFds) {
    task_pid = StartTask();
    EXPECT_TRUE(task_pid > 0);
    int task_open_fds = GetTaskFds();
    StopTask();
    /* The task should have only 1 opened fd (i.e fd 2) */
    EXPECT_EQ(1, task_open_fds);
};

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    int result = RUN_ALL_TESTS();
    return result;
}
