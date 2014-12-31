/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

/*
 * This header file contains C++ language helpers used by every class
 * in the system. DO NOT add anything here that can be thought of as a
 * library or that requires the inclusion on header files.
 */
#ifndef __UTIL_H__
#define __UTIL_H__

#include <boost/function.hpp>

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

#endif /* UTIL_H_ */
