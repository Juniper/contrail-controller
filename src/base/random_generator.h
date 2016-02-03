//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef BASE_RANDOM_GENERATOR_H_
#define BASE_RANDOM_GENERATOR_H_

#include <boost/uuid/random_generator.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
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

class UniformInt8RandomGenerator {
 public:
    UniformInt8RandomGenerator(uint8_t min, uint8_t max) : rgen_(min, max) {}

    uint8_t operator()() {
        tbb::mutex::scoped_lock lock(mutex_);
        return rgen_(rng_);
    }

 private:
    boost::random::mt19937 rng_;
    boost::random::uniform_int_distribution<uint8_t> rgen_;
    tbb::mutex mutex_;
};

#endif  // BASE_RANDOM_GENERATOR_H_
