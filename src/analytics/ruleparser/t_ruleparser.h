/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef T_RULEENG_H
#define T_RULEENG_H

#include <string>
#include <vector>
#include <iostream>
#include <unistd.h>
#include "boost/lexical_cast.hpp"
#include <boost/scoped_ptr.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "ruleutil.h"
#include "t_doc.h"
#include "../viz_message.h"

#include "syslog.h"

/**
 * t_rulemsgtype - class to carry msgtype given in a rule
 * it will also optionally have context
 */
struct t_rulemsgtype {
    t_rulemsgtype(std::string msgtype) : msgtype_(msgtype) {
        has_context_ = false;
    }

    t_rulemsgtype(char* msgtype) : msgtype_(msgtype) {
        has_context_ = false;
    }

    t_rulemsgtype(std::string msgtype, std::string context) :
        msgtype_(msgtype), context_(context) {
            has_context_ = true;
    }

    t_rulemsgtype(char* msgtype, char* context) :
        msgtype_(msgtype), context_(context) {
            has_context_ = true;
    }

    bool operator==(const t_rulemsgtype& rhs) {
        return ((msgtype_ == rhs.msgtype_) &
                (has_context_ == rhs.has_context_) &
                (!has_context_ ||
                 context_ == rhs.context_));
    }

    bool has_context_;
    std::string msgtype_;
    std::string context_;
};

typedef enum {
    RANGEVALUE_S = 1,
    RANGEVALUE_D
} RANGEVALUE_TYPE;

struct t_rangevalue_base {
    t_rangevalue_base(RANGEVALUE_TYPE type) : type_(type) {}
    virtual ~t_rangevalue_base() {}
    RANGEVALUE_TYPE type_;

    virtual bool range_check(const std::string& type, const std::string& value) = 0;
};

struct t_rangevalue_s : public t_rangevalue_base {
    t_rangevalue_s(std::string rangevalue1) :
        t_rangevalue_base(RANGEVALUE_S),
        rangevalue1_(rangevalue1) {}
    ~t_rangevalue_s() {}

    virtual bool range_check(const std::string& type, const std::string& value) {
        if (type == "string") {
            return (rangevalue1_ == value);
        } else if ((type == "i16") ||
                (type == "i32")) {
            int val1 = boost::lexical_cast<int>(value);
            int val2 = boost::lexical_cast<int>(rangevalue1_);
            return (val1 == val2);
        }
        return false;
    }

    std::string rangevalue1_;
};

struct t_rangevalue_d : public t_rangevalue_base {
    t_rangevalue_d(std::string rangevalue1, std::string rangevalue2) :
        t_rangevalue_base(RANGEVALUE_D),
        rangevalue1_(rangevalue1), rangevalue2_(rangevalue2) {}
    ~t_rangevalue_d() {}

    virtual bool range_check(const std::string& type, const std::string& value) {
        if (type == "string") {
            return false;
        } else if ((type == "i16") ||
                (type == "i32")) {
            int val = boost::lexical_cast<int>(value);
            int val1 = boost::lexical_cast<int>(rangevalue1_);
            int val2 = boost::lexical_cast<int>(rangevalue2_);
            return (val > val1 && val < val2);
        }
        return false;
    }

    std::string rangevalue1_;
    std::string rangevalue2_;
};

class t_rangevalue {
    public:
        t_rangevalue() {
        }
        ~t_rangevalue() {}

        void add_rangevalue(t_rangevalue_base* elem) {
            rangevalue_v.push_back(elem);
        }

        void print(std::ostream& os) {
            boost::ptr_vector<t_rangevalue_base>::const_iterator iter;

            os << "[";
            bool first = true;
            for (iter = rangevalue_v.begin(); iter != rangevalue_v.end(); iter++) {
                if (first) {
                    first = false;
                } else {
                    os << ", ";
                }
                if ((iter)->type_ == RANGEVALUE_S) {
                    t_rangevalue_s *value = (t_rangevalue_s *)(&(*iter));
                    os << (value)->rangevalue1_;
                } else if ((iter)->type_ == RANGEVALUE_D) {
                    t_rangevalue_d *value = (t_rangevalue_d *)(&(*iter));
                    os << (value)->rangevalue1_;
                    os << " - ";
                    os << (value)->rangevalue2_;
                }
            }
            os << "]";
        }
        bool range_check(const std::string& type, const std::string& value) {
            boost::ptr_vector<t_rangevalue_base>::iterator it;
            for (it = rangevalue_v.begin(); it != rangevalue_v.end(); it++) {
                if ((it)->range_check(type, value))
                    return true;
            }
            return false;
        }

    private:
        boost::ptr_vector<t_rangevalue_base> rangevalue_v;
};

class t_cond_base {
    public:
        t_cond_base(std::string fieldid) : fieldid_(fieldid) {}
        virtual ~t_cond_base() {}
        virtual void print(std::ostream& os) = 0;
        virtual bool rule_match(const RuleMsg& rmsg) = 0;

    protected:
        std::string fieldid_;
};

class t_cond_range : public t_cond_base {
    public:
    t_cond_range(std::string fieldid, t_rangevalue* value) :
        t_cond_base(fieldid), rangevalue_(value) {
    }
    ~t_cond_range() {}

    virtual void print(std::ostream& os) {
        os << "    (" << fieldid_ << " in ";
        rangevalue_->print(os);
        os << ")";
    }

    virtual bool rule_match(const RuleMsg& rmsg) {
        std::string type, value;
        int ret = rmsg.field_value(fieldid_, type, value);

        if (!ret) {
            return rangevalue_->range_check(type, value);
        }
        return false;
    }

    private:
        boost::scoped_ptr<t_rangevalue> rangevalue_;
};

class t_cond_simple : public t_cond_base {
    public:
    t_cond_simple(std::string fieldid, char op, std::string value) :
        t_cond_base(fieldid), value_(value), operation_(op) {
    }
    ~t_cond_simple() {}

    virtual void print(std::ostream& os) {
        os << "    (" << fieldid_ << " " << operation_ << " " << value_ << ")";
    }

    virtual bool rule_match(const RuleMsg& rmsg) {
        std::string type, value;
        int ret = rmsg.field_value(fieldid_, type, value);

        if (!ret) {
            if (type == "string") {
                if (operation_ == '=') {
                    return (value_ == value);
                }
            } else if ((type == "i16") ||
                    (type == "i32")) {
                int val1 = boost::lexical_cast<int>(value);
                int val2 = boost::lexical_cast<int>(value_);
                if (operation_ == '=') {
                    return (val1 == val2);
                } else if (operation_ == '<') {
                    return (val1 < val2);
                } else if (operation_ == '>') {
                    return (val1 > val2);
                }
            }
        }
        return false;
    }

    private:
        std::string value_;
        char   operation_;
};

class t_rulecondlist {
    public:
        t_rulecondlist() {}
        ~t_rulecondlist() {}

        void add_field(t_cond_base *cond) {
            conditions_.insert(conditions_.begin(), cond);
        }

        void print(std::ostream& os) {
            boost::ptr_vector<t_cond_base>::iterator iter;
            bool first = true;
            for (iter = conditions_.begin(); iter != conditions_.end(); iter++) {
                if (first) {
                    first = false;
                } else {
                    os << " and\n";
                }
                iter->print(os);
            }
            os << "\n";
        }

        bool rule_match(const RuleMsg& rmsg) {
            boost::ptr_vector<t_cond_base>::iterator iter;
            for (iter = conditions_.begin(); iter != conditions_.end(); iter++) {
                if (!((iter)->rule_match(rmsg)))
                    return false;
            }
            return true;
        }

    private:
        boost::ptr_vector<t_cond_base> conditions_;
};

class t_ruleaction {
    public:
        t_ruleaction() {
        }
        ~t_ruleaction() {}

        std::string get_actionid() {
            return actionid_;
        }

        void set_actionid(std::string actionid) {
            actionid_ = actionid;
        }

        void set_actionid(char* actionid) {
            actionid_ = actionid;
        }

        void add_actionparam(std::string elem) {
            paramlist_.push_back(elem);
        }

        void add_actionparam(char* elem) {
            paramlist_.push_back(elem);
        }

        void print(std::ostream& os) {
            os << "action " << actionid_; 
            std::vector<std::string>::const_iterator iter;
            for (iter = paramlist_.begin(); iter != paramlist_.end(); iter++) {
                os << " " << (*iter);
            }
            os << "\n";
        }

        void execute(const RuleMsg& rmsg);

        static std::string RuleActionEchoResult;

    private:
        std::string parse_python_exception();
        std::string actionid_;
        std::vector<std::string> paramlist_;
};

class t_ruleactionlist {
    public:
        t_ruleactionlist() {}
        ~t_ruleactionlist() {}

        void add_action(t_ruleaction* action) {
            boost::ptr_vector<t_ruleaction>::iterator iter;
            for (iter = actions_.begin(); iter != actions_.end(); iter++) {
                if (action->get_actionid() == (iter)->get_actionid()) {
                    LOG(DEBUG, "Duplicate action \n");
                    delete action;
                    return;
                }
            }
            actions_.insert(actions_.begin(), action);
        }

        void print(std::ostream& os) {
            boost::ptr_vector<t_ruleaction>::iterator iter;
            for (iter = actions_.begin(); iter != actions_.end(); iter++) {
                (iter)->print(os);
            }
        }

        void execute(const RuleMsg& rmsg) {

            boost::ptr_vector<t_ruleaction>::iterator iter;
            for (iter = actions_.begin(); iter != actions_.end(); iter++) {
                (iter)->execute(rmsg);
            }
        }

    private:
        boost::ptr_vector<t_ruleaction> actions_;
};

/**
 * t_rule - full definition of a rule along with
 * its conditions and actions
 *
 */
class t_rule : public t_doc {
    public:
        t_rule(std::string name) :
            rulename_(name) {
        }
        ~t_rule() {}

        t_rule(t_rulemsgtype *rulemsgtype, t_rulecondlist *condlist, t_ruleactionlist *actionlist) :
            rulemsgtype_(rulemsgtype), condlist_(condlist), actionlist_(actionlist) {
        }

        void set_name(std::string name) {
            rulename_ = name;
        }

        std::string get_name() {
            return rulename_;
        }

        std::string get_name() const {
            return rulename_;
        }

        void print(std::ostream& os) {
            os << "Rule " << rulename_ << " :\n";
            if (rulemsgtype_->has_context_) {
                os << "For ((msgtype eq " << rulemsgtype_->msgtype_ << ") and (context eq " << rulemsgtype_->context_ << "))";
            } else {
                os << "For msgtype eq " << rulemsgtype_->msgtype_;
            }

            if (condlist_) {
                os << " match\n";
                condlist_->print(os);
            } else {
                os << "\n";
            }

            actionlist_->print(os);
        }

        bool rule_present(const t_rulemsgtype& m) {
            return ((rulemsgtype_->msgtype_ == m.msgtype_) &
                    (rulemsgtype_->has_context_ == m.has_context_) &
                    (!rulemsgtype_->has_context_ ||
                     rulemsgtype_->context_ == m.context_));
        }

        bool rule_present(const t_rulemsgtype& m) const {
            return ((rulemsgtype_->msgtype_ == m.msgtype_) &
                    (rulemsgtype_->has_context_ == m.has_context_) &
                    (!rulemsgtype_->has_context_ ||
                     rulemsgtype_->context_ == m.context_));
        }

        void rule_execute(const RuleMsg& rmsg) {
            if (!((rulemsgtype_->msgtype_ == rmsg.messagetype) &&
                ((!rulemsgtype_->has_context_ && !rmsg.hdr.__isset.Context) ||
                 (rulemsgtype_->has_context_ && rmsg.hdr.__isset.Context && rulemsgtype_->context_ == rmsg.hdr.Context)))) {
                return;
            }

            if (!condlist_ || condlist_->rule_match(rmsg)) {
                if (actionlist_)
                    actionlist_->execute(rmsg);
            }
        }

        void rule_execute(const RuleMsg& rmsg) const {
            if (!((rulemsgtype_->msgtype_ == rmsg.messagetype) &&
                ((!rulemsgtype_->has_context_ && !rmsg.hdr.__isset.Context) ||
                 (rulemsgtype_->has_context_ && rmsg.hdr.__isset.Context && rulemsgtype_->context_ == rmsg.hdr.Context)))) {
                return;
            }

            if (!condlist_ || condlist_->rule_match(rmsg)) {
                if (actionlist_)
                    actionlist_->execute(rmsg);
            }
        }

    private:
        std::string rulename_;
        boost::scoped_ptr<t_rulemsgtype> rulemsgtype_;
        boost::scoped_ptr<t_rulecondlist> condlist_;
        boost::scoped_ptr<t_ruleactionlist> actionlist_;
};

/**
 * t_rulelist consists of all rules parsed in a file
 *
 */
class t_rulelist: public t_doc {
    public:
        t_rulelist(std::string path):
            path_(path),
            name_(program_name(path)) {
        }

        t_rulelist() {}
        ~t_rulelist() {}

        void add_rule(t_rule* rule) {
            boost::ptr_vector<t_rule>::iterator iter;
            for (iter = rules_.begin(); iter != rules_.end(); iter++) {
                if (rule->get_name() == iter->get_name()) {
                    LOG(DEBUG, "Duplicate rule \n");
                    delete rule;
                    return;
                }
            }
            rules_.push_back(rule);
        }

        boost::ptr_vector<t_rule>& get_rules() {
            return rules_;
        }

        void print(std::ostream& os) {
            boost::ptr_vector<t_rule>::iterator iter;
            for (iter = rules_.begin(); iter != rules_.end(); iter++) {
                (iter)->print(os);
            }
        }

        bool rule_present(const t_rulemsgtype& msgtype) {
            boost::ptr_vector<t_rule>::iterator iter;
            for (iter = rules_.begin(); iter != rules_.end(); iter++) {
                if ((iter)->rule_present(msgtype)) {
                    return true;
                }
            }
            return false;
        }

        bool rule_execute(const RuleMsg& rmsg) {
            t_ruleaction::RuleActionEchoResult.clear();

            boost::ptr_vector<t_rule>::iterator iter;
            for (iter = rules_.begin(); iter != rules_.end(); iter++) {
                (iter)->rule_execute(rmsg);
            }
            return true;
        }

    private:
        // File path
        std::string path_;

        // Name
        std::string name_;

        // vector of all rules
        boost::ptr_vector<t_rule> rules_;
};

#endif
