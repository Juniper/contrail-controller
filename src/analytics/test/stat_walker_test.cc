/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "base/logging.h"

#include "stat_walker.h"
#include <boost/assign/list_of.hpp>

using std::vector;
using std::string;
using std::pair;
using std::make_pair;
using boost::assign::map_list_of;

struct ArgSet {
    std::string statAttr;
    DbHandler::TagMap attribs_tag;
    DbHandler::AttribMap attribs;
};

class StatCbTester {
public:

    StatCbTester(const vector<ArgSet>& exp) : exp_(exp) {
        for (vector<ArgSet>::const_iterator it = exp_.begin();
             it != exp_.end(); it++ ) {
            match_.push_back(false);
        }
    }

    void Cb(const uint64_t &timestamp,
            const std::string& statName,
            const std::string& statAttr,
            const DbHandler::TagMap & attribs_tag,
            const DbHandler::AttribMap & attribs) {
        bool is_match = false;
        for (DbHandler::TagMap::const_iterator ct = attribs_tag.begin();
             ct != attribs_tag.end(); ct++) {
            LOG (ERROR, "tag " << ct->first);
        }
        for (DbHandler::AttribMap::const_iterator ct = attribs.begin();
             ct != attribs.end(); ct++) {
            LOG (ERROR, "attrib " << ct->first);
        }
        for (size_t idx = 0 ; idx < exp_.size() ; idx ++) {
            if ((exp_[idx].statAttr == statAttr) && 
                (exp_[idx].attribs_tag == attribs_tag) &&
                (exp_[idx].attribs == attribs)) {
                EXPECT_EQ(match_[idx] , false);
                match_[idx] = true;
                is_match = true;
                break;
            }
        }
        //(void)is_match;
        EXPECT_EQ(true, is_match);
    }

    virtual ~StatCbTester() {
        for (vector<bool>::const_iterator it = match_.begin();
             it != match_.end(); it++ ) {
            EXPECT_EQ(true, *it);
        }
    }

    const vector<ArgSet> exp_;
    vector<bool> match_;
            
};

class StatWalkerTest: public ::testing::Test {
public:
    StatWalkerTest() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
    virtual ~StatWalkerTest() {}

};

TEST_F(StatWalkerTest, Simple) {
    string statName = string("Simple");
    vector<ArgSet> av1;
    ArgSet a1;
    a1.statAttr = string("virt");
    a1.attribs = map_list_of(
        "name", DbHandler::Var("a6s40:MyProc"))(
        "Source", DbHandler::Var("a6s40"))(
        "virt.mem", DbHandler::Var((uint64_t)1000000));

    DbHandler::AttribMap sm;
    a1.attribs_tag.insert(make_pair("name", make_pair(DbHandler::Var("a6s40:MyProc"), sm)));
    a1.attribs_tag.insert(make_pair("Source", make_pair(DbHandler::Var("a6s40"), sm)));
    av1.push_back(a1);
         
    StatCbTester ct(av1);
    
    StatWalker::TagMap m1;
    StatWalker::TagVal h1,h2;
    h1.val = string("a6s40:MyProc");
    m1.insert(make_pair(string("name"), h1));
    h2.val = string("a6s40");
    m1.insert(make_pair(string("Source"), h2));

    StatWalker::TagMap m2;
    StatWalker sw(boost::bind(&StatCbTester::Cb, &ct, _1, _2, _3, _4, _5), 0, statName, m1);
    DbHandler::AttribMap attribs = map_list_of("mem", DbHandler::Var((uint64_t)1000000));
    sw.Push("virt", m2, attribs);
    sw.Pop(); 
}

TEST_F(StatWalkerTest, SingleTag) {
    string statName = string("SingleTag");
    vector<ArgSet> av1;
    ArgSet a1;
    a1.statAttr = string("virt");
    a1.attribs = map_list_of(
        "name", DbHandler::Var("a6s40:MyProc"))(
        "Source", DbHandler::Var("a6s40"))(
        "virt.mem", DbHandler::Var((uint64_t)1000000))(
        "virt.bank", DbHandler::Var("bank1"));

    DbHandler::AttribMap sm;
    a1.attribs_tag.insert(make_pair("name", make_pair(DbHandler::Var("a6s40:MyProc"), sm)));
    a1.attribs_tag.insert(make_pair("Source", make_pair(DbHandler::Var("a6s40"), sm)));
    a1.attribs_tag.insert(make_pair("virt.bank", make_pair(DbHandler::Var("bank1"), sm)));
    av1.push_back(a1);
         
    StatCbTester ct(av1);
    
    StatWalker::TagMap m1;
    StatWalker::TagVal h1,h2;
    h1.val = string("a6s40:MyProc");
    m1.insert(make_pair(string("name"), h1));
    h2.val = string("a6s40");
    m1.insert(make_pair(string("Source"), h2));

    StatWalker::TagMap m2;
    StatWalker sw(boost::bind(&StatCbTester::Cb, &ct, _1, _2, _3, _4, _5), 0, statName, m1);
    DbHandler::AttribMap attribs = map_list_of(
        "mem", DbHandler::Var((uint64_t)1000000))(
        "bank", string("bank1"));
    StatWalker::TagVal tv1;
    tv1.val = string("bank1");
    m2.insert(make_pair("bank", tv1));
    sw.Push("virt", m2, attribs);
    sw.Pop(); 
}

TEST_F(StatWalkerTest, DoubleTag) {
    string statName = string("DoubleTag");
    vector<ArgSet> av1;
    ArgSet a1;
    a1.statAttr = string("virt");
    a1.attribs = map_list_of(
        "name", DbHandler::Var("a6s40:MyProc"))(
        "Source", DbHandler::Var("a6s40"))(
        "table", DbHandler::Var("table1"))(
        "virt.mem", DbHandler::Var((uint64_t)1000000))(
        "virt.bank", DbHandler::Var("bank1"));

    DbHandler::AttribMap sm;
    a1.attribs_tag.insert(make_pair("name", make_pair(DbHandler::Var("a6s40:MyProc"), sm)));
    a1.attribs_tag.insert(make_pair("Source", make_pair(DbHandler::Var("a6s40"), sm)));
    sm.insert(make_pair("virt.bank", DbHandler::Var("bank1")));
    a1.attribs_tag.insert(make_pair("table", make_pair(DbHandler::Var("table1"), sm)));
    av1.push_back(a1);
         
    StatCbTester ct(av1);
    
    StatWalker::TagMap m1;
    StatWalker::TagVal h1,h2;
    h1.val = string("a6s40:MyProc");
    m1.insert(make_pair(string("name"), h1));
    h2.val = string("a6s40");
    m1.insert(make_pair(string("Source"), h2));

    StatWalker::TagMap m2;
    StatWalker sw(boost::bind(&StatCbTester::Cb, &ct, _1, _2, _3, _4, _5), 0, statName, m1);
    DbHandler::AttribMap attribs = map_list_of(
        "mem", DbHandler::Var((uint64_t)1000000))(
        "bank", string("bank1"));
    StatWalker::TagVal tv1;
    tv1.val = string("bank1");
    tv1.prefix = make_pair("table", DbHandler::Var("table1"));
    m2.insert(make_pair("bank", tv1));
    sw.Push("virt", m2, attribs);
    sw.Pop(); 
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
