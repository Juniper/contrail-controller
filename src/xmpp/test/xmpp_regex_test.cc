/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "control-node/control_node.h"
#include "base/test/task_test_util.h"
#include "xmpp/xmpp_state_machine.h"
#include "xmpp/xmpp_session.h"
#include "xmpp/xmpp_str.h"

#include "base/logging.h"
#include "base/util.h"
#include "xmpp/xmpp_config.h"

#include "testing/gunit.h"

using namespace std;

class XmppRegexMock : public XmppSession {
public:
    XmppRegexMock(TcpServer *server, Socket *sock) : 
                  XmppSession(server, sock), p1("<(iq|message)"), bufx_("") { }
    ~XmppRegexMock() { }

    //boost::regex Regex() { return p1; }
    void SetRegex(const char *ss) { p1 = ss; }

    void AppendString(const string &str) {
        bufx_ += str;
        SetBuf(str);
    }

    void SetString(const string &str) {
        bufx_ = str;
        ReplaceBuf(bufx_);
    }

    int MatchTest() {
        int ret = this->MatchRegex(p1);
        return ret;
    }

    const char *TagStr(uint8_t i) {
        tag_ = string(res_[i].first, res_[i].second);
        return tag_.c_str();
    }

    const char *FromOffset() {
        string::const_iterator end = buf_.end();
        tag_ = string(offset_, end); 
        return tag_.c_str();
    }

    const char *Buf() {
        string::const_iterator st = buf_.begin();
        tag_ = string(st, offset_); 
        return tag_.c_str();
    }

private:
    boost::regex p1;
    string bufx_;
    string tag_;
};

class XmppRegexTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        regex_.reset(new XmppRegexMock(NULL, NULL));
    }

    virtual void TearDown() {
    }


    auto_ptr<XmppRegexMock> regex_;
};

namespace {

TEST_F(XmppRegexTest, Connection) {
    string str("<iq what =1><comm> blah </comm> </iq>");
    string tag;

    // basic test...
    regex_->SetString(str);
    
    // full match
    int ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 0);
    ASSERT_STREQ(regex_->TagStr(0), "<iq"); 
    //std::cout << " Matching string : " << regex_->TagStr(0) << std::endl;

    // expect no match
    regex_->SetString(str);
    regex_->SetRegex("<bbl");
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == -1);

    regex_->SetString(str);
    regex_->SetRegex("</iq>t"); // partial match
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 1);
    //std::cout << " Matching string : " << regex_->TagStr(0) << std::endl;

    str = "<?xml version='1.0'?><stream:stream iq = '2\"><tag1> document blah </tag1> </stream:stream>";
    regex_->SetString(str);

    regex_->SetRegex("(<?.*?>)(<stream:stream\\s*iq\\s*=\\s*[\"'].*[\"'])");
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 0);
    //std::cout << " Matching string : " << regex_->TagStr(2) << std::endl;
    const char *match = "<stream:stream iq = '2\"";
    ASSERT_STREQ(regex_->TagStr(2), match);

    regex_->SetString(str);
    regex_->SetRegex(rXMPP_STREAM_START);
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 0);
    ASSERT_STREQ(regex_->TagStr(0), "<?xml version='1.0'?><stream:stream");

    str = "<iq a = '2'> <item> blah blah </item></iq>";
    regex_->SetString(str);
    regex_->SetRegex(rXMPP_MESSAGE);
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 0);
    ASSERT_STREQ(regex_->TagStr(0), "<iq");

    str = "<message a = '2'> <item> blah blah </item></message>";
    regex_->SetString(str);
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 0);
    ASSERT_STREQ(regex_->TagStr(0), "<message");

    //partial match
    str = "<message a = '2'> <item> blah blah </item></mess";
    regex_->SetString(str);
    regex_->SetRegex("</(iq|message)>");
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 1);
    ASSERT_STREQ(regex_->TagStr(0), "</mess");
    ASSERT_STREQ(regex_->FromOffset(), "</mess");

    str = "age><iq a = '2'> <item>";
    regex_->AppendString(str);
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 0);
    ASSERT_STREQ(regex_->TagStr(0), "</message>");

    // no match
    str = "<message a = '2'> ";
    regex_->SetString(str);
    regex_->SetRegex(rXMPP_MESSAGE);
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 0);
    ASSERT_STREQ(regex_->TagStr(0), "<message");

    str = "<item> blah blah ";
    regex_->AppendString(str);
    regex_->SetRegex("</(iq|message)>");
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == -1);
    str = "</item></message><somejunk>";
    regex_->AppendString(str);
    ret = regex_->MatchTest();
    EXPECT_TRUE(ret == 0);
    ASSERT_STREQ(regex_->TagStr(0), "</message>");
    ASSERT_STREQ(regex_->Buf(), "<message a = '2'> <item> blah blah </item></message>");
}

}
static void SetUp() {
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
