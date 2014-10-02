//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef BASE_RANDOM_GENERATOR_H_
#define BASE_RANDOM_GENERATOR_H_

#include <boost/uuid/random_generator.hpp>
#include <tbb/mutex.h>

class ThreadSafeUuidGenerator {
 public:
    boost::uuids::uuid operator()() {
        tbb::mutex::scoped_lock lock(mutex_);
        return rgen_();
    }

 private:
    boost::uuids::random_generator rgen_;
    tbb::mutex mutex_;
};

#endif  // BASE_RANDOM_GENERATOR_H_
