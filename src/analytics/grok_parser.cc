/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "grok_parser.h"
#include <iostream>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <vector>
#include <list>
//#include <boost/thread.hpp>

/*
typedef boost::shared_mutex shared_mutex;
typedef boost::shared_lock<shared_mutex> shared_lock;
typedef boost::unique_lock<shared_mutex> unique_lock;
shared_mutex mutex;

typedef std::list<boost::shared_ptr<boost::thread> > ThreadPtrList;
ThreadPtrList threads;
boost::mutex matched_list_mutex;

class PriorityLock {
public:
    void LowPriorityLock() {
        L.lock();
        N.lock();
        M.lock();
        N.unlock();
    }

    void LowPriorityUnlock() {
        M.unlock();
        L.unlock();
    }

    void HighPriorityLock() {
        N.lock();
        M.lock();
        N.unlock();
    }

    void HighPriorityUnlock() {
        M.unlock();
    }

private:
    boost::mutex M;
    boost::mutex N;
    boost::mutex L;
};

PriorityLock pl;
bool stop_sig;
*/

GrokParser::GrokParser() {
    init();
}

GrokParser::~GrokParser() {
    /*
    unique_lock lock(mutex);
    for (GrokMap::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        grok_free_clone(&(it->second.first));
    }
    lock.unlock();
    */
    grok_free(base_);
    free(base_);
}

/* init() -- Initialize GrokParser */
void GrokParser::init() {
    base_ = grok_new();
    /* Load base pattern from file */
    grok_patterns_import_from_file(base_, "/etc/contrail/grok-pattern-base.conf");
    //stop_sig = false;
}

/*
void GrokParser::msg_enqueue_wrapper(std::string strin) {
    boost::shared_ptr<boost::thread> t(new boost::thread(&GrokParser::msg_enqueue, this, strin));
}
*/

/* msg_enqueue() -- Enqueue input message to message queue */
/* @input - strin: input message */
/*
void GrokParser::msg_enqueue(std::string strin) {
    if (!stop_sig) {
        pl.HighPriorityLock();
        msg_queue_.push_back(std::make_pair(strin, IDLE));
        pl.HighPriorityUnlock();
    }
}
*/

/* non-blocking process_queue */
/*
void GrokParser::process_queue_wrapper() {
    boost::thread t(&GrokParser::process_queue, this);
}
*/

/* process_queue() 
   Repeatedly check the STATUS of threads of matching threads.
*/
/*
void GrokParser::process_queue() {
    while (true) {
        if (stop_sig) {
            for (ThreadPtrList::iterator it = threads.begin(); it != threads.end(); it++) {
                if ((*it).get()->joinable()) {
                    (*it).get()->join();
                }
                (*it).reset();
            }
            return;
        }
        pl.LowPriorityLock();
        if (!msg_queue_.empty()) {
            STATUS status = msg_queue_.front().second;
            if (status == IDLE) {
                std::pair<std::string, STATUS> tmp = msg_queue_.front();
                tmp.second = PROCESS;
                msg_queue_.pop_front();
                msg_queue_.push_back(tmp);
                boost::shared_ptr<boost::thread> t(new boost::thread(&GrokParser::match_wrapper, this, tmp.first));
                //boost::thread t(&GrokParser::match_wrapper, this, tmp.first);
                threads.push_back(t);
            }
            else if (status == PROCESS) {
                std::pair<std::string, STATUS> tmp = msg_queue_.front();
                msg_queue_.pop_front();
                msg_queue_.push_back(tmp);
            }
            else if (status == NOMATCH) {
                std::pair<std::string, STATUS> tmp = msg_queue_.front();
                tmp.second = PROCESS;
                msg_queue_.pop_front();
                msg_queue_.push_back(tmp);
                boost::shared_ptr<boost::thread> t(new boost::thread(&GrokParser::match_wrapper, this, tmp.first));
                //boost::thread t(&GrokParser::match_wrapper, this, tmp.first);
                threads.push_back(t);
            }
            else { //status == MATCH
                msg_queue_.pop_front();
            }
        }
        pl.LowPriorityUnlock();
    }
}
*/

/* add_base_pattern()
*  Add a base pattern to base_grok
*  @Input - pattern: input pattern w/ format <NAME regex>
*/
void GrokParser::add_base_pattern(std::string pattern) {
    grok_patterns_import_from_string(base_, pattern.c_str());
}

/* msg_type_add()
*  Create a new grok instance with defined message type and add to map
*  @Input - s: Syntax only
*  @Return - true: created
*          - false: non-exist pattern or invalid pattern
*/
bool GrokParser::msg_type_add(std::string s) {
    const char *regexp = NULL;
    size_t len = 0;
    /* find message type in base pattern */
    grok_pattern_find(base_, s.c_str(), s.length(), &regexp, &len);
    if (regexp == NULL) {
        std::cout << "[msg_type_add:" << __LINE__ << "] > Failed to create grok instance. Syntax NOT DEFINED." << std::endl;
        return false;
    }

    /* Assign grok->patterns to the subtree */
    grok_t grok;
    grok_clone(&grok, base_);

    /* try compile */
    if (GROK_OK != grok_compile(&grok, std::string("%{" + s + "}").c_str())) {
        grok_free_clone(&grok);
        std::cout << "[msg_type_add:" << __LINE__ << "] > Failed to create grok instance. Syntax INVALID." << std::endl;
        return false;
    }

    std::set<std::string> m;
    if (!_pattern_name_add_to_set(std::string(regexp), &m)) {
        std::cout<< "[msg_type_add:" << __LINE__ << "] > Failed to create grok instance. Subname in pattern repeated?" << std::endl;
        grok_free_clone(&grok);
        return false;
    }
    /* add to map */
    //unique_lock lock(mutex);
    grok_list_[s] = std::make_pair(grok, m);
    std::cout << "[msg_type_add:" << __LINE__ << "] > Syntax <" << s << "> added." << std::endl;
    //lock.unlock();
    return true;
}

/* _pattern_name_add_to_set()
* traverse through message type and identify named capture
* @Input s -- input string,  m -- map of message type:value. default value is the  empty string ""
*/
bool GrokParser::_pattern_name_add_to_set(std::string s,
                              std::set<std::string>* m) {
    boost::regex ex("%\\{(\\w+)(:(\\w+))?\\}");
    boost::smatch match;
    boost::sregex_token_iterator iter(s.begin(), s.end(), ex, 0);
    boost::sregex_token_iterator end;
    for (; iter != end; ++iter) {
            std::string r = *iter;
            std::vector<std::string> results;
            boost::split(results, r, boost::is_any_of(":"));
            boost::algorithm::trim_left_if(results[0], boost::is_any_of("%{"));
            boost::algorithm::trim_right_if(results[(int)results.size()-1], boost::is_any_of("}"));
            if (results.size() == 1) {
                const char *regexp = NULL;
                size_t len = 0;
                grok_pattern_find(base_, results[0].c_str(), results[0].length(), &regexp, &len);
                if (regexp != NULL) {
                    bool b = _pattern_name_add_to_set(std::string(regexp), m);
                    if (!b) return b;
                }
            }
            else if (results.size() == 2) {
                std::pair<std::set<std::string>::iterator, bool> p = m->insert(results[1]);
                if (!p.second) return false;
            }
    }
    return true;
}

/* msg_type_del()
*  Delete message type and corresponding grok instance 
*  @Input - s; message type to delete
*/
bool GrokParser::msg_type_del(std::string s) {
    //unique_lock lock(mutex);
    GrokMap::iterator it = grok_list_.find(s);
    if (it == grok_list_.end()) {
        std::cout<< "[msg_type_del:" << __LINE__ << "] > Failed to delete. Grok instance with provided message type does not exist." << std::endl;
        //lock.unlock();
        return false;
    }
    grok_free_clone(&(it->second.first));
    grok_list_.erase(it);
    //lock.unlock();
    return true;
}

/* match_wrapper()
*  Instiantiate a thread of match() and write STATUS to message queue depending on match result 
*  @Input - strin: input message
*/
/*
void GrokParser::match_wrapper(std::string strin) {
    std::string retval = match(strin);
    if ("" == retval) {
        pl.HighPriorityLock();
        for (MsgQueue::iterator itt = msg_queue_.begin(); itt != msg_queue_.end(); itt++) {
            if (itt->first == strin) {
                itt->second = NOMATCH;
                //std::cout<< "[match_wrapper:" << __LINE__ << "] > NOMATCH " << std::endl;
            }
        }
        pl.HighPriorityUnlock();
    }
    else {
        pl.HighPriorityLock();
        for (MsgQueue::iterator itt = msg_queue_.begin(); itt != msg_queue_.end(); itt++) {
            if (itt->first == strin) {
                itt->second = MATCH;
                //std::cout << "[match:" << __LINE__ << "] > Message Type: " << retval << std::endl;
            }
        }
        pl.HighPriorityUnlock();
    }
}
*/

/* match()
*  Match input message against all stored message type patterns and identify message type
*  @Input - strin: input message
                m: map to store matched content
*/
std::string GrokParser::match(std::string strin) {
    //shared_lock lock(mutex);
    std::string ret;
    for (GrokMap::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        grok_match_t gm;
        if (grok_exec(&(it->second.first), strin.c_str(), &gm) == GROK_OK) {
            const char *match;
            int len(0);
            ret = it->first;
            //std::cout << "[match:" << __LINE__ << "] > Message Type: " << ret << std::endl;
            /* construct map */
            std::map<std::string, std::string> m;
            m["Type"] = ret; 
            for (std::set<std::string>::iterator itt = it->second.second.begin();
                     itt != it->second.second.end(); itt++) {
                grok_match_get_named_substring(&gm, (*itt).c_str(), &match, &len);
                //std::cout << (*itt) << " ==> " << std::string(match).substr(0,len) << std::endl;
                m[(*itt)] = std::string(match).substr(0,len);
            }
            //matched_list_mutex.lock();
            matched_msg_list_[strin] = m;
            //matched_list_mutex.unlock();
            return ret;
        }
    }
    return ret;
}

/*
void GrokParser::process_stop() {
    stop_sig = true;
}
*/

/* get_matched_data()
   get matched content for a message type
   @Input - s: message type to match
            m: map to store matched content
*/
void GrokParser::get_matched_data(std::string strin, std::map<std::string, std::string> * m) {
    //matched_list_mutex.lock();
    if (m != NULL) {
        MatchedMsgList::iterator it = matched_msg_list_.find(strin);
        if (it != matched_msg_list_.end()) {
            (*m) = it->second;
        }
        else {
            m = NULL;
        }
    }
    //matched_list_mutex.unlock();
}

void GrokParser::list_grok() {
    //shared_lock lock(mutex);
    for (GrokMap::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        std::cout << it->first << std::endl;
    }
}
