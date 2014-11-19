#include "base/os.h"
#include <cmn/agent_cmn.h>

#include "testing/gunit.h"
#include <boost/filesystem.hpp>
#include <boost/lambda/bind.hpp>
#include <sstream>


using namespace std;
class Agent;

class InstanceTaskFdTest : public ::testing::Test {
public:
    virtual void SetUp() {
    	agent_ = Agent::GetInstance();
    	ofstream tesfile;
		testfile.open("loop_test.sh");
		testfile << "while true; do sleep 1; done";
		testfile.close();

		std::stringstream cmd_str;
		cmd_str << "/bin/sh" << " loop_test.sh";
		task_ = new InstanceTask(cmd_str.str(), Start,
								 agent_->event_manager());
    }

    virtual void TearDown() {
    	delete task_;
    	remove("loop_test.sh");
    }

    int StartTask() {
    	/* create few fds */
    	int task_pid;
    	if(pipe(pipe_fds) == -1)
    		return -1;
    	sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    	if (sock_fd < 0)
    		return -1
    	/* run the task now */
    	task_pid = task_->Run();
    	return task_pid;
    }

    int StopTask() {
    	task_->Stop();
    	close(pipe_fds[0]);
    	close(pipe_fds[1]);
    	close(sock_fd);
    }

    int GetTaskFds() {
    	using namespace boost::filesystem;
    	using namespace boost::lambda;
    	std::stringstream proc_path;
    	proc_path << "/proc/" <<task_->pid<<"/fd/"
    	path the_path(proc_path.str());
    	no_of_fds = std::count_if(directory_iterator(the_path),
    	        				  directory_iterator(),
    	        				  bind( static_cast<bool(*)(const path&)>(is_other),
    	        				  bind( &directory_entry::path, _1)));
    	return no_of_fds;
    }

private:
    int pipe_fds[2];
    int sock_fd;
    Agent *agent_;
    InstaceTask *task_;
}

TEST_F(InstanceTaskTest, TestCloseTaskFds) {
	task_pid = StartTask();
	EXPECT_TRUE(task_pid > 0);
	actual_task_fds = GetTaskFds();
	StopTask();
	EXPECT_EQ(1U, GetTaskFds();)
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
