/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/scoped_array.hpp>
#include <cassert>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include <string>
#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <boost/python.hpp>

// Careful: must include globals first for extern definitions
#include "../ruleutil.h"
#include "../t_ruleparser.h"
#include "../ruleglob.h"

#include "testing/gunit.h"
#include "base/logging.h"
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h> 
#include <sandesh/sandesh_trace.h> 
#include "../../viz_message.h"

SandeshTraceBufferPtr UVETraceBuf(SandeshTraceBufferCreate("UveTrace", 25000));
/**
 * Global time string
 */
char* g_time_str;

/**
 * Diplays the usage message and then exits with an error code.
 */
void usage() {
    fprintf(stderr, "Usage: rulegen file\n");
    exit(1);
}

/** Set to true to debug docstring parsing */
bool dump_docs = true;

/**
 * Dumps docstrings to stdout
 * Only works for top-level definitions and the whole program doc
 * (i.e., not enum constants, struct fields, or functions.
 */
void dump_docstrings(t_rulelist* rulelist) {
    std::string progdoc = rulelist->get_doc();
    if (!progdoc.empty()) {
        printf("Whole program doc:\n%s\n", progdoc.c_str());
    }
    boost::ptr_vector<t_rule>& rules = rulelist->get_rules();
    boost::ptr_vector<t_rule>::iterator t_iter;
    for (t_iter = rules.begin(); t_iter != rules.end(); ++t_iter) {
        t_rule* rule = &(*t_iter);
        if (rule->has_doc()) {
            printf("rule %s:\n%s\n", rule->get_name().c_str(), rule->get_doc().c_str());
        }
    }
}

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
"action echoaction Rule3 STATS_MSG matched\n"
"Rule Rule4 :\n"
"For msgtype eq SYSLOG_MSG1 match\n"
"    (field1 = field1_value) and\n"
"    (field3 in [10, 20, 30]) and\n"
"    (field2.field21 in [2000 - 5000, 5500, 6000 - 7000])\n"
"action echoaction.py Rule4 matched\n"
"action raise_alarm Alarm1";

std::string filename;
bool out2coutg;

class RuleParserTest : public ::testing::Test {
public:
    virtual void SetUp() {
        if (!filename.empty()) {
            std::ifstream is;
            is.open (filename.c_str(), std::ios::in );

            // get length of file:
            is.seekg (0, std::ios::end);
            length = is.tellg();
            is.seekg (0, std::ios::beg);

            // allocate memory:
            buffer.reset(new char [length+2]);

            // read data as a block:
            is.read(buffer.get(),length);

            is.close();
        } else {
            std::istringstream ss(rules);
            ss.seekg (0, std::ios::end);
            length = ss.tellg();
            ss.seekg (0, std::ios::beg);
            //
            // allocate memory:
            buffer.reset(new char [length+2]);
            ss.read(buffer.get(),length);
        }

        out2cout = out2coutg;
    }

    virtual void TearDown() {
    }

    boost::scoped_array<char> buffer;
    int length;
    bool out2cout;
};

TEST_F(RuleParserTest, Test1) {
    boost::scoped_array<char> localbuf(new char [length+2]);
    t_rulelist *rulelist = new t_rulelist();

    bcopy(buffer.get(), localbuf.get(), length);
    localbuf.get()[length] = localbuf.get()[length+1] = 0;
    parse(rulelist, localbuf.get(), (size_t)(length+2));

    if (out2cout) {
        rulelist->print(std::cout);
    }

    std::ostringstream ss2;
    rulelist->print(ss2);

    int result = bcmp(buffer.get(), ss2.str().c_str(), length);
    EXPECT_EQ(result, 0);

    delete rulelist;
}

TEST_F(RuleParserTest, Test2) {
    t_rulelist *rulelist = new t_rulelist();

    parse(rulelist, (const char *)buffer.get(), length);

    if (out2cout) {
        rulelist->print(std::cout);
    }

    std::ostringstream ss2;
    rulelist->print(ss2);

    int result = bcmp(buffer.get(), ss2.str().c_str(), length);
    EXPECT_EQ(result, 0);

    delete rulelist;
}

TEST_F(RuleParserTest, RuleActionTest) {
    t_rulelist *rulelist = new t_rulelist();

    parse(rulelist, (const char *)buffer.get(), length);

    SandeshHeader hdr;
    std::string messagetype("SYSLOG_MSG");
    hdr.Context = "123456";
    hdr.__isset.Context = true;
    std::string xmlmessage("<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">2121</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">30</field3></VNSwitchErrorMsg></Sandesh>");
    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp1(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg1(vmsgp1);

    rulelist->rule_execute(rmsg1);

    EXPECT_EQ(" echoaction Rule1 matched",
            t_ruleaction::RuleActionEchoResult);

    messagetype = "STATS_MSG";
    hdr.Context.clear();
    hdr.__isset.Context = false;;
    xmlmessage = "<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21></field2><field3 type=\"i32\">30</field3></VNSwitchErrorMsg></Sandesh>";
    unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp2(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg2(vmsgp2);

    rulelist->rule_execute(rmsg2);

    EXPECT_EQ(" echoaction Rule2 STATS_MSG matched",
            t_ruleaction::RuleActionEchoResult);

    messagetype = "STATS_MSG";
    hdr.Context.clear();
    hdr.__isset.Context = false;;
    xmlmessage = "<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">21</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">30</field3></VNSwitchErrorMsg></Sandesh>";
    unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp3(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg3(vmsgp3);

    rulelist->rule_execute(rmsg3);

    EXPECT_EQ(" echoaction Rule2 STATS_MSG matched echoaction Rule3 STATS_MSG matched",
            t_ruleaction::RuleActionEchoResult);

    delete rulelist;
}

TEST_F(RuleParserTest, RuleActionTestPy) {
    t_rulelist *rulelist = new t_rulelist();

    parse(rulelist, (const char *)buffer.get(), length);

    SandeshHeader hdr;
    std::string messagetype("SYSLOG_MSG1");
    hdr.Context.clear();
    hdr.__isset.Context = false;
    std::string xmlmessage("<Sandesh><VNSwitchErrorMsg type=\"sandesh\"><length type=\"i32\">0000000020</length><field1 type=\"string\">field1_value</field1><field2 type=\"struct\"><field21 type=\"i16\">2121</field21><field22 type=\"string\">string22</field22></field2><field3 type=\"i32\">30</field3></VNSwitchErrorMsg></Sandesh>");
    boost::uuids::uuid unm = boost::uuids::random_generator()();
    boost::shared_ptr<VizMsg> vmsgp1(new VizMsg(hdr, messagetype, xmlmessage, unm)); 
    RuleMsg rmsg1(vmsgp1);

    rulelist->rule_execute(rmsg1);

    EXPECT_EQ(" echoaction.py Rule4 matched",
            t_ruleaction::RuleActionEchoResult);

    delete rulelist;
}

int main(int argc, char **argv) {
    int a = 1;
    while (a < argc) {
        // Real-pathify it
        if (!strcmp(argv[a], "-o")) {
            out2coutg = true;
        }

        if (!strcmp(argv[a], "-f")) {
            a++;
            char rp[PATH_MAX];
            if (saferealpath(argv[a], rp) == NULL) {
                failure("Could not open input file with realpath: %s", argv[1]);
            }
            std::string input_file(rp);
            filename = input_file;
        }

        a++;
    }

    Py_Initialize();

    // insert the patch where scripts are placed
    // temporary it is env variable RULEENGPATH
    char *rpath = getenv("RULEENGPATH");
    if (rpath != NULL) {
        PyObject* sysPath = PySys_GetObject((char*)"path");
        PyList_Insert(sysPath, 0, PyString_FromString(rpath));
    }

    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
