/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include <cstdlib>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/generator_iterator.hpp>

#include "base/util.h"
#include "base/logging.h"
#include "base/task.h"
#include "base/parse_object.h"
#include "base/contrail_ports.h"
#include "base/test/task_test_util.h"
#include "io/test/event_manager_test.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_http.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_session.h>
#include <sandesh/sandesh_client.h>

#include "../viz_collector.h"
#include "../ruleeng.h"
#include "../generator.h"
#include "Python.h"
//#include <boost/python.hpp>
#include "test/vizd_test_types.h"
#include "hiredis/hiredis.h" 
#include "gendb_if.h"
#include "viz_constants.h"
#include "hiredis/hiredis.h" 

using boost::assign::list_of;
using std::string;
using std::vector;
using ::testing::Return;
using ::testing::AnyNumber;

string sourcehost = "127.0.0.1";
string collector_server = "127.0.0.1";

#define WAIT_FOR(Cond)                  \
do {                                    \
    for (int i = 0; i < 10000; i++) {  \
        if (Cond) break;                \
        usleep(1000);                   \
    }                                   \
    EXPECT_TRUE(Cond);                  \
} while (0)

std::string exec(char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while(!feof(pipe)) {
        if(fgets(buffer, 128, pipe) != NULL)
            result += buffer;
    }
    pclose(pipe);
    return result;
}

void SetRedisPath() {
    char *rpath = getenv("LD_LIBRARY_PATH");
    if (NULL == rpath) rpath = getenv("DYLD_LIBRARY_PATH");
    if (rpath != NULL) {
        string ppath(rpath);
        ppath.append("/../../src/analytics/test/utils/mockredis/");

        PyObject* sysPath = PySys_GetObject((char*)"path");
        PyObject *pPath = PyString_FromString(ppath.c_str());
        PyList_Insert(sysPath, 0, pPath);
        Py_DECREF(pPath);
    }  
}

bool StartRedisSentinel(int port, int redis_port) {
    SetRedisPath();
    PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue1, *pValue2;

    pName = PyString_FromString("mockredis");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    pFunc = PyObject_GetAttrString(pModule, "start_redis_sentinel");
    pValue1 = PyInt_FromLong(port);
    pValue2 = PyInt_FromLong(redis_port);
    pArgs = PyTuple_New(2);
    PyTuple_SetItem(pArgs, 0, pValue1);
    PyTuple_SetItem(pArgs, 1, pValue2);
    PyObject_CallObject(pFunc, pArgs);
    Py_DECREF(pArgs);
    Py_DECREF(pValue1);
    Py_DECREF(pValue2);
    Py_DECREF(pFunc);
    Py_DECREF(pModule);
    return true;
}

bool StopRedisSentinel(int port) {
    PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue;

    pName = PyString_FromString("mockredis");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    pFunc = PyObject_GetAttrString(pModule, "stop_redis_sentinel");
    pValue = PyInt_FromLong(port);
    pArgs = PyTuple_New(1);
    PyTuple_SetItem(pArgs, 0, pValue);
    PyObject_CallObject(pFunc, pArgs);
    Py_DECREF(pArgs);
    Py_DECREF(pValue);
    Py_DECREF(pFunc);
    Py_DECREF(pModule);
    return true;
}

bool StopRedis(int port) {
    char *rpath = getenv("LD_LIBRARY_PATH");
    if (NULL == rpath) rpath = getenv("DYLD_LIBRARY_PATH");
    if (rpath != NULL) {
        string ppath(rpath);
        ppath.append("/../../src/analytics/test/utils/mockredis/");

        PyObject* sysPath = PySys_GetObject((char*)"path");
        PyObject *pPath = PyString_FromString(ppath.c_str());
        PyList_Insert(sysPath, 0, pPath);
        Py_DECREF(pPath);
    }  
    PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue;

    pName = PyString_FromString("mockredis");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    pFunc = PyObject_GetAttrString(pModule, "stop_redis");
    pValue = PyInt_FromLong(port);
    pArgs = PyTuple_New(1);
    PyTuple_SetItem(pArgs, 0, pValue);
    PyObject_CallObject(pFunc, pArgs);
    Py_DECREF(pArgs);
    Py_DECREF(pValue);
    Py_DECREF(pFunc);
    Py_DECREF(pModule);
    return true;
}

bool StartRedis(int port) {
    char *rpath = getenv("LD_LIBRARY_PATH");
    if (NULL == rpath) rpath = getenv("DYLD_LIBRARY_PATH");
    if (rpath != NULL) {
        string ppath(rpath);
        ppath.append("/../../src/analytics/test/utils/mockredis/");

        PyObject* sysPath = PySys_GetObject((char*)"path");
        PyObject *pPath = PyString_FromString(ppath.c_str());
        PyList_Insert(sysPath, 0, pPath);
        Py_DECREF(pPath);
    }  
    PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue;

    pName = PyString_FromString("mockredis");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    pFunc = PyObject_GetAttrString(pModule, "start_redis");
    pValue = PyInt_FromLong(port);
    pArgs = PyTuple_New(1);
    PyTuple_SetItem(pArgs, 0, pValue);
    PyObject_CallObject(pFunc, pArgs);
    Py_DECREF(pArgs);
    Py_DECREF(pValue);
    Py_DECREF(pFunc);
    Py_DECREF(pModule);
    return true;
}

class GeneratorTest {
public:
    GeneratorTest(unsigned short port) : 
        evm_(new EventManager()),
        thread_(new ServerThread(evm_.get())) {
        Sandesh::InitGenerator("VRouterAgent", sourcehost, evm_.get(),
                               0, NULL);
        Sandesh::ConnectToCollector(collector_server, port);

        thread_->Start();

        WAIT_FOR(Sandesh::client()->state() == SandeshClientSM::ESTABLISHED);
    }

    ~GeneratorTest() {
        task_util::WaitForIdle();
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }

    void Shutdown() {
        task_util::WaitForIdle();
        Sandesh::Uninit();
    }

    void SendMessageUVETrace() {
        UveVirtualNetworkConfig uvevn;
        uvevn.set_name("abc-corp:vn02");
        uvevn.set_total_interfaces(10);
        uvevn.set_total_virtual_machines(5);
        uvevn.set_total_acl_rules(60);

        std::vector<std::string> vcn;
        uvevn.set_connected_networks(vcn);
        UveVirtualNetworkConfigTrace::Send(uvevn);

        UveVirtualNetworkAgent uvena;
        uvena.set_name("abc-corp:vn02");
        uvena.set_in_tpkts(40);
        uvena.set_total_acl_rules(55);

        std::vector<UveInterVnStats> vvn;
        UveInterVnStats vnstat;
        vnstat.set_other_vn("abc-corp:map-reduce-02");
        vnstat.set_tpkts(10);
        vnstat.set_bytes(1200);
        vvn.push_back(vnstat);

        uvena.set_in_stats(vvn);
        UveVirtualNetworkAgentTrace::Send(uvena);
    }

    std::auto_ptr<EventManager> evm_;
    std::auto_ptr<ServerThread> thread_;
};

class VizRedisTest : public ::testing::Test {
public:
    VizRedisTest() : 
        evm_(new EventManager()),
        thread_(new ServerThread(evm_.get())),
        dbif_(GenDb::GenDbIf::GenDbIfImpl(evm_.get()->io_service(), boost::bind(&VizRedisTest::DbErrorHandlerFn, this),
                "127.0.0.1", 9191, false, 0, "127.0.0.1:VizRedisTest")) {
        {
            boost::asio::ip::tcp::endpoint endpoint1(boost::asio::ip::tcp::v4(), 0);
            boost::asio::ip::tcp::acceptor acceptor1(*evm_.get()->io_service(), endpoint1);
            redis_sentinel_port_ = acceptor1.local_endpoint().port(); 
            boost::asio::ip::tcp::endpoint endpoint2(boost::asio::ip::tcp::v4(), 0);
            boost::asio::ip::tcp::acceptor acceptor2(*evm_.get()->io_service(), endpoint2);
            redis_port_ = acceptor2.local_endpoint().port(); 
        }

        StartRedisSentinel(redis_sentinel_port_, redis_port_);
        StartRedis(redis_port_);
        DbHandler *db_handler(new DbHandler(dbif_));
        OpServerProxy * osp = new OpServerProxy(evm_.get(), 0, string("127.0.0.1"),
                                                redis_sentinel_port_);
        Ruleeng *ruleeng(new Ruleeng(db_handler, osp));
        collector_ = new Collector(evm_.get(), 0, db_handler, ruleeng);
        collector_port_ = collector_->GetPort();
        LOG(DEBUG, "Starting Collector on port " << collector_port_);
        analytics_.reset(new VizCollector(evm_.get(), db_handler, ruleeng,
            collector_, osp));

        thread_->Start();
    }

    ~VizRedisTest() {
        task_util::WaitForIdle();
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
        analytics_.reset(NULL);
        StopRedis(redis_port_);
        StopRedisSentinel(redis_sentinel_port_);
    }

    void DbErrorHandlerFn() {
        assert(0);
    }    

    virtual void SetUp() {
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        analytics_->Shutdown();
        task_util::WaitForIdle();
        Sandesh::Uninit();
        task_util::WaitForIdle();
    }

    unsigned short collector_port_;
    unsigned short redis_sentinel_port_;
    unsigned short redis_port_;
    std::auto_ptr<EventManager> evm_;
    std::auto_ptr<ServerThread> thread_;
    GenDb::GenDbIf *dbif_;
    std::auto_ptr<VizCollector> analytics_;
    Collector *collector_;    
};

TEST_F(VizRedisTest, SendUVE) {
    analytics_->Init();
    GeneratorTest gentest(collector_port_);

    task_util::WaitForIdle();

    usleep(1000000);
    
    gentest.SendMessageUVETrace();

    usleep(1000000);
    
    task_util::WaitForIdle();

    redisContext *c = redisConnect("127.0.0.1", redis_port_);
    ASSERT_FALSE(c->err);

    redisReply * reply = (redisReply *) redisCommand(c, "hget %s total_interfaces",
        "VALUES:ObjectVNTable:abc-corp:vn02:127.0.0.1:VRouterAgent:UveVirtualNetworkConfig");
    ASSERT_FALSE(c->err);
    ASSERT_NE(reply, (redisReply *)NULL);

    EXPECT_EQ(reply->type, REDIS_REPLY_STRING);
    freeReplyObject(reply);
    redisFree(c);
    gentest.Shutdown();

}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    Py_Initialize();
    int ret = RUN_ALL_TESTS();
    Py_Finalize();
    return ret;
}

