/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "t_ruleparser.h"
#include <boost/python.hpp>
#include <boost/python/object.hpp>
#include <boost/python/handle.hpp>

void t_ruleaction::execute(const RuleMsg& rmsg) {
    if (actionid_ == "echoaction") {
        RuleActionEchoResult.append(" ");
        RuleActionEchoResult.append(actionid_);

        std::vector<std::string>::const_iterator iter;
        for (iter = paramlist_.begin(); iter != paramlist_.end(); iter++) {
            RuleActionEchoResult.append(" ");
            RuleActionEchoResult.append((*iter));
        }
    }

    if (actionid_ == "echoaction.py") {
        using namespace boost::python;
        object main_module = import("__main__");
        dict main_namespace = static_cast<dict>(main_module.attr("__dict__"));

        char *rpath = getenv("RULEENGPATH");
        std::string commandstr;
        if (rpath != NULL) {
            commandstr.append(rpath);
            commandstr.append("/echoaction.py");
        } else {
            commandstr.append("echoaction.py");
        }

        if (access(commandstr.c_str(), R_OK)) {
            std::cout << "File error: " << commandstr << std::endl;

            RuleActionEchoResult.append(" ");
            RuleActionEchoResult.append(actionid_);

            std::vector<std::string>::const_iterator iter;
            for (iter = paramlist_.begin(); iter != paramlist_.end(); iter++) {
                RuleActionEchoResult.append(" ");
                RuleActionEchoResult.append((*iter));
            }

            return;
        }

        boost::python::str command(commandstr);
        list param_list;
        std::vector<std::string>::const_iterator iter;
        for (iter = paramlist_.begin(); iter != paramlist_.end(); iter++) {
            param_list.append((*iter));
        }
        main_namespace["paramlist"] = param_list;
        main_namespace["PYTHONPATH"] = "/usr/lib/python2.7/site-packages:/Users/rajreddy/Documents/Work/Dev/ctrlplane/src/analytics/ruleeng";

        std::string cmdouts;
        object cmdout;
        try {
            cmdout = exec_file(command, main_namespace, main_namespace);
        } catch(error_already_set const &){
            std::string perror_str = parse_python_exception();
            std::cout << "Error in Python exec_file: " << perror_str << std::endl;
        }

        try {
            if (main_namespace.has_key("actionresult"))
                cmdouts = extract<std::string>(main_namespace["actionresult"]);
            else
                cmdouts = "actionresultNone";
        } catch(error_already_set const &) {
            std::string perror_str = parse_python_exception();
            std::cout << "Error in boost extract: " << perror_str << std::endl;
        }
        RuleActionEchoResult.append(" ");
        RuleActionEchoResult.append(cmdouts);
    }
    syslog(LOG_ERR, "%s action called\n", actionid_.c_str());
}

std::string t_ruleaction::parse_python_exception() {
    namespace py = boost::python;

    PyObject *type_ptr = NULL, *value_ptr = NULL, *traceback_ptr = NULL;
    PyErr_Fetch(&type_ptr, &value_ptr, &traceback_ptr);
    std::string ret("Unfetchable Python error");

    if(type_ptr != NULL) {
        py::handle<> h_type(type_ptr);
        py::str type_pstr(h_type);
        py::extract<std::string> e_type_pstr(type_pstr);
        if(e_type_pstr.check())
            ret = e_type_pstr();
        else
            ret = "Unknown exception type";
    }

    if(value_ptr != NULL) {
        py::handle<> h_val(value_ptr);
        py::str a(h_val);
        py::extract<std::string> returned(a);
        if(returned.check())
            ret +=  ": " + returned();
        else
            ret += std::string(": Unparseable Python error: ");
    }

    if(traceback_ptr != NULL){
        py::handle<> h_tb(traceback_ptr);
        py::object tb(py::import("traceback"));
        py::object fmt_tb(tb.attr("format_tb"));
        py::object tb_list(fmt_tb(h_tb));
        py::object tb_str(py::str("\n").join(tb_list));
        py::extract<std::string> returned(tb_str);
        if(returned.check())
            ret += ": " + returned();
        else
            ret += std::string(": Unparseable Python traceback");
    }
    return ret;
}

