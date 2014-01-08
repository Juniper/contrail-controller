/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/function.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/asio/ip/address.hpp>
#include <sstream>
#include <stdlib.h>
#include <sys/time.h>
#include <vector>

#define DISALLOW_COPY_AND_ASSIGN(_Class) \
	_Class(const _Class &);				\
	_Class& operator=(const _Class &)


#ifdef NDEBUG
#define CHECK_INVARIANT(Cond)   \
    do {                        \
        if (!(Cond)) {          \
            LOG(WARN, "Invariant failed: " ## Cond);    \
            return false;       \
        }                       \
    } while (0)
#else
#define CHECK_INVARIANT(Cond)   \
    do {                        \
        assert(Cond);           \
    } while (0)
#endif


template <typename IntType>
bool BitIsSet(IntType value, size_t bit) {
    return (value & (1 << bit));
}

template <typename IntType>
void SetBit(IntType &value, size_t bit) {
    value |= (1 << bit);
}

template <typename IntType>
void ClearBit(IntType &value, size_t bit) {
    value &= ~(1 << bit);
}


class ModuleInitializer {
public:
    ModuleInitializer(boost::function<void(void)> func) {
        (func)();
    }
};

// This two levels of indirection is necessary because otherwise preprocessor
// will not expand __LINE__ after ## operator
#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define MODULE_INITIALIZER(Func) \
static ModuleInitializer TOKENPASTE2(init_, __LINE__)(Func);

#define KEY_COMPARE(x, y) \
    do { \
        if ((x) < (y)) return -1; \
        if ((x) > (y)) return 1;  \
    } while(0);

template <typename Container>
void STLDeleteValues(Container *container) {
    typename Container::iterator next;
    for (typename Container::iterator iter = container->begin();
         iter != container->end(); iter = next) {
        next = iter;
        ++next;
        delete *iter;
    }
    container->clear();
}

// Delete all the elements of a map.
template <typename Container> 
void STLDeleteElements(Container *container) {
    typename Container::iterator next;
    for (typename Container::iterator iter = container->begin();
         iter != container->end(); iter = next) {
        next = iter;
        ++next;
        delete iter->second;
    }
    container->clear();
}

// Check if key exist in collection
template <typename Collection, typename T>
bool STLKeyExists(const Collection &col, const T &key) {
    return col.find(key) != col.end();
}

template <typename T>
class custom_ptr {
public:
    custom_ptr(boost::function<void(T *)> deleter, T *ptr = 0)
    : deleter_(deleter), ptr_(ptr) {
        assert(deleter_ != NULL);
    }
    ~custom_ptr() {
        if (ptr_ != NULL) {
            (deleter_)(ptr_);
        }
    }
    T *get() const {
        return ptr_;
    }
    T *operator->() const {
        return ptr_;
    }
    void reset(T *ptr = 0) {
        if (ptr_ != NULL) {
            (deleter_)(ptr_);
        }
        ptr_ = ptr;
    }
    T *release() {
        T *ptr = ptr_;
        ptr_ = NULL;
        return ptr;
    }
private:
    boost::function<void(T *)> deleter_;
    T *ptr_;
};

static boost::posix_time::ptime epoch_ptime(boost::gregorian::date(1970,1,1));

/* timestamp - returns usec since epoch */
static inline uint64_t UTCTimestampUsec() {
    boost::posix_time::ptime t2(boost::posix_time::microsec_clock::universal_time());
    boost::posix_time::time_duration diff = t2 - epoch_ptime; 
    return diff.total_microseconds();
}

static inline boost::posix_time::ptime UTCUsecToPTime(uint64_t tusec) {
    boost::posix_time::ptime pt(boost::gregorian::date(1970, 1, 1), 
                   boost::posix_time::time_duration(0, 0, 
                   tusec/1000000, 
                   boost::posix_time::time_duration::ticks_per_second()/1000000*(tusec%1000000)));
    return pt;
}

static inline std::string UuidToString(const boost::uuids::uuid &id)
{
    std::stringstream uuidstring;
    uuidstring << id;
    return uuidstring.str();
}

static inline boost::uuids::uuid StringToUuid(const std::string &str)
{
    boost::uuids::uuid u = boost::uuids::nil_uuid();
    std::stringstream uuidstring(str);
    uuidstring >> u;
    return u;
}

static inline boost::asio::ip::address_v4 GetIp4SubnetAddress(
              const boost::asio::ip::address_v4 &ip_prefix, uint16_t plen) {
    boost::asio::ip::address_v4 subnet(ip_prefix.to_ulong() & 
                                       (0xFFFFFFFF << (32 - plen)));
    return subnet;
}

static inline bool IsIp4SubnetMember(
              const boost::asio::ip::address_v4 &ip,
              const boost::asio::ip::address_v4 &prefix_ip, uint16_t plen) {
    boost::asio::ip::address_v4 prefix = GetIp4SubnetAddress(prefix_ip, plen);
    return ((prefix.to_ulong() | ~(0xFFFFFFFF << (32 - plen))) ==
                (ip.to_ulong() | ~(0xFFFFFFFF << (32 - plen))));
}

// Writes a number into a string
template <typename NumberType>
static inline const std::string integerToString(const NumberType &num) {
    std::stringstream ss;
    ss << num;
    return ss.str();
}

// int8_t must be handled specially because std::stringstream sees
// int8_t as a text type instead of an integer type
template <>
inline const std::string integerToString<>(const int8_t &num) {
    std::stringstream ss;
    ss << (int16_t)num;
    return ss.str();
}

// uint8_t must be handled specially because std::stringstream sees
// uint8_t as a text type instead of an integer type
template <>
inline const std::string integerToString<>(const uint8_t &num) {
    std::stringstream ss;
    ss << (uint16_t)num;
    return ss.str();
}

// Converts string into a number
template <typename NumberType>
static inline void stringToInteger(const std::string& str, NumberType &num) {
    std::stringstream ss(str);
    ss >> num;
}

// int8_t must be handled properly because stringstream sees int8_t
// as a text type instead of an integer type
template <>
inline void stringToInteger<>(const std::string& str, int8_t &num) {
    int16_t tmp;
    std::stringstream ss(str);
    ss >> tmp;
    assert(tmp > -128 && tmp < 128);
    num = (int8_t)tmp;
}

// uint8_t must be handled properly because stringstream sees uint8_t
// as a text type instead of an integer type
template <>
inline void stringToInteger<>(const std::string& str, uint8_t &num) {
    uint16_t tmp;
    std::stringstream ss(str);
    ss >> tmp;
    assert(tmp < 256);
    num = (uint8_t)tmp;
}

//
// Split a the initial part of the string based on 'seperator' characters
// and return the resultant list of tokens after converting each token into
// NumberType elements
//
template<typename NumberType>
static inline bool stringToIntegerList(std::string input,
                                       std::string seperator,
                                       std::vector<NumberType> &entries) {
    std::vector<std::string> tokens;

    boost::split(tokens, input, boost::is_any_of(seperator),
                 boost::token_compress_on);

    if (!tokens.size()) {
        return false;
    }

    std::vector<std::string>::iterator iter;

    for (iter = tokens.begin(); iter != tokens.end(); iter++) {
        std::stringstream ss(*iter);
        NumberType value;
        ss >> value;

        //
        // Bail if there is an error during the conversion.
        //
        if (ss.fail()) {
            return false;
        }
        entries.push_back(value);
    }

    return true;
}

//
// Utility routines to retrieve and log back trace from stack at run time in
// human readable form
//
class BackTrace {
private:
    static ssize_t ToString(void * const* callstack, int frames, char *buf,
                            size_t buf_len);

public:
    static void Log(std::string msg = "");
    static void Log(void * const* callstack, int frames, std::string msg = "");
    static int Get(void * const* &callstack);
};

static inline const std::string duration_usecs_to_string(const uint64_t usecs) {
    std::ostringstream os;
    boost::posix_time::time_duration duration;

    duration = boost::posix_time::microseconds(usecs);
    os << duration;
    return os.str();
}

//
// Get VN name from routing instance
//
static inline std::string GetVNFromRoutingInstance(const std::string &vn) {
    std::vector<std::string> tokens;
    boost::split(tokens, vn, boost::is_any_of(":"), boost::token_compress_on);
    if (tokens.size() < 3) return "";
    return tokens[0] + ":" + tokens[1] + ":" + tokens[2];
}
#endif /* UTIL_H_ */
