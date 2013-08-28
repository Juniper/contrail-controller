/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/scoped_array.hpp>
#include "testing/gunit.h"
#include "base/logging.h"
#include "../ruleeng.h"

const char *rules =
"Rule Rule1 :\n"
"For ((msgtype eq SYSLOG_MSG) and (context eq 123456)) match\n"
"    (field1 = field1_value) and\n"
"    (field3 in [10, 20, 30]) and\n"
"    (field2.field21 in [2000 - 5000, 5500, 6000 - 7000])\n"
"action echoaction Rule1 matched\n"
"action raise_alarm Alarm1\n"
"Rule Rule2 :\n"
"For msgtype eq STATS_MSG\n"
"action echoaction Rule2 STATS_MSG matched\n"
"Rule Rule3 :\n"
"For msgtype eq STATS_MSG match\n"
"    (field3 = 30) and\n"
"    (field2.field22 = string22)\n"
"action echoaction Rule3 STATS_MSG matched";

class RuleengTest : public ::testing::Test {
public:
    virtual void SetUp() {
        std::istringstream ss(rules);
        ss.seekg (0, std::ios::end);
        length = ss.tellg();
        ss.seekg (0, std::ios::beg);
        //
        // allocate memory:
        boost::scoped_array<uint8_t> b(new char [length]);
        char *b = new char [length];
        ss.read (b.get(), length);
        buffer.assign(b.get(), length);
    }

    virtual void TearDown() {
    }

    std::string buffer;
    int length;
};

TEST_F(RuleengTest, BuildRuleTest) {
    Ruleeng *ruleeng = new Ruleeng(NULL);

    ruleeng->Buildrules("Memorybuffer1", buffer);

    /* Buildrules will launch task to buildrules, wait for
     * that task to finish */
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    while (!scheduler->IsEmpty()) {
    }

    std::ostringstream ss;
    ruleeng->print(ss);

    int result = bcmp(buffer.c_str(), ss.str().c_str(), length);
    EXPECT_EQ(result, 0);

    free(ruleeng);
}

TEST_F(RuleengTest, RuleActionTest) {
    Ruleeng *ruleeng = new Ruleeng(NULL);

    ruleeng->Buildrules("Memorybuffer1", buffer);
    /* Buildrules will launch task to buildrules, wait for
     * that task to finish */
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    while (!scheduler->IsEmpty()) {
    }

    {
        SandeshHeader hdr;
        std::string messagetype("SYSLOG_MSG");
        hdr.Context = "123456";
        hdr.__isset.Context = true;
        std::string xmlmessage = "<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">2121</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">30</field3></VNSwitchErrorMsg></Sandesh>";
        boost::shared_ptr<VizMsg> vmsgp1(new VizMsg(hdr, messagetype, xmlmessage)); 

        Task *task = new Worker(ruleeng, vmsgp1);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
        while (!scheduler->IsEmpty()) {
        }
        EXPECT_EQ(" echoaction Rule1 matched",
                t_ruleaction::RuleActionEchoResult);
    }

    {
        SandeshHeader hdr;
        std::string messagetype("STATS_MSG");
        hdr.Context.clear();
        hdr.__isset.Context = false;
        std::string xmlmessage = "<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21></field2><field3 type=\"i32\">30</field3></VNSwitchErrorMsg></Sandesh>";
        boost::shared_ptr<VizMsg> vmsgp2(new VizMsg(hdr, messagetype, xmlmessage)); 

        Task *task = new Worker(ruleeng, vmsgp2);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
        while (!scheduler->IsEmpty()) {
        }
        EXPECT_EQ(" echoaction Rule2 STATS_MSG matched",
                t_ruleaction::RuleActionEchoResult);
    }

    {
        SandeshHeader hdr;
        std::string messagetype = "STATS_MSG";
        hdr.Context.clear();
        hdr.__isset.Context = false;
        std::string xmlmessage = "<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">30</field3></VNSwitchErrorMsg></Sandesh>";
        boost::shared_ptr<VizMsg> vmsgp3(new VizMsg(hdr, messagetype, xmlmessage)); 
        Task *task = new Worker(ruleeng, vmsgp3);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
        while (!scheduler->IsEmpty()) {
        }
        EXPECT_EQ(" echoaction Rule2 STATS_MSG matched echoaction Rule3 STATS_MSG matched",
                t_ruleaction::RuleActionEchoResult);
    }

    free(ruleeng);
}

class RuleengObjTrTest : public Ruleeng, public ::testing::Test {
public:
    explicit RuleengObjTrTest() : Ruleeng(NULL) {
    }

    ~RuleengObjTrTest() {
    }


    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    void set_expect_vec(std::vector<std::pair<std::string, std::string> > vec) {
        expect_vec = vec;
    }

    void reset_iteration() {
        iteration = 0;
    }

    virtual bool handle_uvesend(const std::string &type, const std::string &source,
            const std::string &key, const std::string &message) { return true;}
    virtual bool handle_uvedelete(const std::string &type, const std::string &source,
            const std::string &key) { return true;}
    virtual void insert_into_table(const std::string table, const std::string rowkey,
            const RuleMsg& rmsg, const boost::uuids::uuid& unm) {
        EXPECT_STREQ(table.c_str(), expect_vec[iteration].first.c_str());
        EXPECT_STREQ(rowkey.c_str(), expect_vec[iteration].second.c_str());
        iteration++;
    }
    virtual bool insert_into_flow_table(const RuleMsg& rmsg) { return true; }

private:
    static int iteration;
    std::vector<std::pair<std::string, std::string> > expect_vec;
};
int RuleengObjTrTest::iteration = 0;

TEST_F(RuleengObjTrTest, NoKeys) {
    {
        SandeshHeader hdr;
        std::string messagetype("VirtualNetwork");
        std::string xmlmessage = "<Sandesh><VirtualNetwork type=\"sandesh\"><length type=\"i32\">0000000040</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">2121</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">30</field3></VirtualNetwork></Sandesh>";
        boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage)); 

        std::pair<std::string, std::string> expect_arr[] = {};

        std::vector<std::pair<std::string, std::string> > expect_vec(expect_arr,
                expect_arr + sizeof(expect_arr)/sizeof(std::pair<std::string, std::string>));

        set_expect_vec(expect_vec);
        reset_iteration();
        Task *task = new Worker(this, vmsgp);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
        while (!scheduler->IsEmpty()) {
        }
    }
}

TEST_F(RuleengObjTrTest, OneKey) {
    {
        SandeshHeader hdr;
        std::string messagetype("VirtualNetwork");
        std::string xmlmessage = "<Sandesh><VirtualNetwork type=\"sandesh\"><length type=\"i32\">0000000040</length><field1 type=\"string\" key=\"TestTable1\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">2121</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">30</field3></VirtualNetwork></Sandesh>";
        boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage)); 

        std::pair<std::string, std::string> expect_arr[] = {
            std::pair<std::string, std::string>("TestTable1", "field1_value") };
        std::vector<std::pair<std::string, std::string> > expect_vec(expect_arr,
                expect_arr + sizeof(expect_arr)/sizeof(std::pair<std::string, std::string>));

        set_expect_vec(expect_vec);
        reset_iteration();
        Task *task = new Worker(this, vmsgp);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
        while (!scheduler->IsEmpty()) {
        }
    }
}

TEST_F(RuleengObjTrTest, MultiKey) {
    {
        SandeshHeader hdr;
        std::string messagetype("VirtualNetwork");
        std::string xmlmessage = "<Sandesh><VirtualNetwork type=\"sandesh\"><length type=\"i32\">0000000040</length><field1 type=\"string\" key=\"TestTable1\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\" key=\"TestTable2\">2121</field21><field22 type=\"string\" key=\"TestTable2\">string22</field22></field2><field3 type=\"i32\">30</field3></VirtualNetwork></Sandesh>";
        boost::shared_ptr<VizMsg> vmsgp(new VizMsg(hdr, messagetype, xmlmessage)); 

        std::pair<std::string, std::string> expect_arr[] = {
            std::pair<std::string, std::string>("TestTable1", "field1_value"),
            std::pair<std::string, std::string>("TestTable2", "2121:string22") };
        std::vector<std::pair<std::string, std::string> > expect_vec(expect_arr,
                expect_arr + sizeof(expect_arr)/sizeof(std::pair<std::string, std::string>));

        set_expect_vec(expect_vec);
        reset_iteration();
        Task *task = new Worker(this, vmsgp);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
        while (!scheduler->IsEmpty()) {
        }
    }
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

