#include "base/os.h"
#include <base/task.h>
#include <base/util.h>
#include <cfg/cfg_init.h>
#include <cmn/agent_cmn.h>
#include <io/event_manager.h>
#include <oper/interface_common.h>
#include <oper/operdb_init.h>
#include <oper/vm.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/mirror_table.h>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/document.h"
#include "rapidjson/reader.h"
#include <test/test_cmn_util.h>
#include "testing/gunit.h"
#include "vr_types.h"
#include <vrouter/ksync/ksync_init.h>

using namespace std;
using namespace boost::assign;

#define VNSW_NEXTHOP_SERVER_CONFIG_FILE \
    "controller/src/vnsw/agent/nexthop_server/test/vnswa_cfg.ini"

class CfgTest : public ::testing::Test {
public:

    ~CfgTest() {
        TestAgentInit *init = client_->agent_init();
        init->Shutdown();
        AsioStop();
    }

    void SetUp() {
    }

    void TearDown() {
        WAIT_FOR(1000, 1000, agent_->vrf_table()->Size() == 1);
    }

    void WaitForIdle(uint32_t size, uint32_t max_retries) {
        do {
            client_->WaitForIdle();
        } while ((agent_->nexthop_table()->Size() == size) &&
                 max_retries-- > 0);
    }

    void CreateTunnelNH(const string &vrf_name, const Ip4Address &sip,
                        const Ip4Address &dip, bool policy,
                        TunnelType::TypeBmap bmap) {
        DBRequest req;
        TunnelNHData *data = new TunnelNHData();
        uint32_t table_size = agent_->nexthop_table()->Size();

        NextHopKey *key = new TunnelNHKey(vrf_name, sip, dip, policy,
                                          TunnelType::ComputeType(bmap));
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(key);
        req.data.reset(data);
        agent_->nexthop_table()->Enqueue(&req);
        WaitForIdle(table_size, 5);
    }

    void DeleteTunnelNH(const string &vrf_name, const Ip4Address &sip,
                        const Ip4Address &dip, bool policy,
                        TunnelType::TypeBmap bmap) {
        DBRequest req;
        TunnelNHData *data = new TunnelNHData();
        uint32_t table_size = agent_->nexthop_table()->Size();

        NextHopKey *key = new TunnelNHKey(vrf_name, sip, dip, policy,
                                          TunnelType::ComputeType(bmap));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(key);
        req.data.reset(data);
        agent_->nexthop_table()->Enqueue(&req);
        WaitForIdle(table_size, 5);
    }

    Agent *agent_;
    std::auto_ptr<TestClient> client_;
};

static void
ValidateRcvdMessage(const u_int8_t *rcv_msg, int rcv_len, int exp_len,
                    const char *exp_ip, const char *exp_action)
{
    char rcv_json[256];

    TASK_UTIL_EXPECT_EQ(exp_len, rcv_len);
    int msg_len = rcv_msg[0] << 24 | rcv_msg[1] << 16 | rcv_msg[2] << 8 | rcv_msg[3];
    TASK_UTIL_EXPECT_EQ((exp_len - 4), msg_len);
    memcpy(rcv_json, &rcv_msg[4], msg_len);
    contrail_rapidjson::Document d;
    d.Parse<0>((const char *)rcv_json);
    TASK_UTIL_EXPECT_TRUE(d.IsObject());
    TASK_UTIL_EXPECT_TRUE(d.HasMember(exp_ip));
    TASK_UTIL_EXPECT_TRUE(d[exp_ip].HasMember("action"));
    TASK_UTIL_EXPECT_EQ(0, strcmp(exp_action,
                                  d[exp_ip]["action"].GetString()));
}

class NexthopClient {
 public:
    explicit NexthopClient(const std::string &path) :
        sock_path_(path),
        socket_(-1) {
    }

    ~NexthopClient() {
        if (socket_ != -1) {
            close(socket_);
        }
    }

    bool Connect() {
        socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(socket_ != -1);
        struct sockaddr_un remote;
        memset(&remote, 0, sizeof(remote));
        remote.sun_family = AF_UNIX;
        strncpy(remote.sun_path, sock_path_.c_str(), sizeof(remote.sun_path));
        int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
        int res = connect(socket_, (sockaddr *) &remote, len);
        return (res != -1);
    }

    int Send(const u_int8_t *data, size_t len) {
        return send(socket_, data, len, 0);
    }

    int Recv(u_int8_t *buffer, size_t len) {
        return recv(socket_, buffer, len, 0);
    }

    void Close() {
        int res = shutdown(socket_, SHUT_RDWR);
        assert(res == 0);
    }

  private:
    std::string sock_path_;
    int socket_;
};

TEST_F(CfgTest, TunnelNH) {
    client_.reset(TestInit(VNSW_NEXTHOP_SERVER_CONFIG_FILE, false));
    agent_ = client_->agent();
    AddEncapList(agent_, "MPLSoUDP", "MPLSoGRE", NULL);

    client_->WaitForIdle();

    if (!agent_->params() ||
        !agent_->params()->nexthop_server_endpoint().length()) {
        LOG(ERROR, "nexthop server endpoint not specified");
        FAIL();
    }

    NexthopClient uclient(agent_->params()->nexthop_server_endpoint());
    TASK_UTIL_EXPECT_TRUE(uclient.Connect());

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   ((1 << TunnelType::MPLS_GRE) | (1 << TunnelType::MPLS_UDP)));

    u_int8_t rcv_msg[256];
    int len;

    len = uclient.Recv(rcv_msg, sizeof(rcv_msg));
    ValidateRcvdMessage(rcv_msg, len, 34, "10.1.1.10", "add");

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    len = uclient.Recv(rcv_msg, sizeof(rcv_msg));
    ValidateRcvdMessage(rcv_msg, len, 34, "10.1.1.11", "add");

    CreateTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));

    len = uclient.Recv(rcv_msg, sizeof(rcv_msg));
    ValidateRcvdMessage(rcv_msg, len, 34, "10.1.1.12", "add");

    client_->WaitForIdle();

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.10"),
                                     false, TunnelType::MPLS_UDP) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.11"),
                                     false, TunnelType::MPLS_GRE) == true));

    WAIT_FOR(100, 100, (TunnelNHFind(Ip4Address::from_string("10.1.1.12"),
                                     false, TunnelType::MPLS_UDP) == true));

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.10"), false,
                   (1 << TunnelType::MPLS_UDP));

    len = uclient.Recv(rcv_msg, sizeof(rcv_msg));
    ValidateRcvdMessage(rcv_msg, len, 34, "10.1.1.10", "del");

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.11"), false,
                   (1 << TunnelType::MPLS_GRE));

    len = uclient.Recv(rcv_msg, sizeof(rcv_msg));
    ValidateRcvdMessage(rcv_msg, len, 34, "10.1.1.11", "del");

    DeleteTunnelNH(agent_->fabric_vrf_name(), agent_->router_id(),
                   Ip4Address::from_string("10.1.1.12"), false,
                   (1 << TunnelType::MPLS_UDP));

    len = uclient.Recv(rcv_msg, sizeof(rcv_msg));
    ValidateRcvdMessage(rcv_msg, len, 34, "10.1.1.12", "del");

    client_->WaitForIdle();

    DelEncapList(agent_);
    client_->WaitForIdle();
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    return ret;
}
