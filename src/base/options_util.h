//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#ifndef BASE_OPTIONS_UTIL_H_
#define BASE_OPTIONS_UTIL_H_

#include <vector>
#include <algorithm>
#include <sstream>
#include <iterator>

#include <boost/program_options.hpp>

namespace options {
namespace util {

// Implementation overloads
template <typename ElementType>
bool GetOptValueImpl(
    const boost::program_options::variables_map &var_map,
    std::vector<ElementType> &var, const std::string &val,
    std::vector<ElementType>*, bool if_not_defaulted) {
    // Check if the value is present.
    if (var_map.count(val) && (!if_not_defaulted ||
        (if_not_defaulted && !var_map[val].defaulted()))) {
        std::vector<ElementType> tmp(
            var_map[val].as<std::vector<ElementType> >());
        // Now split the individual elements
        for (typename std::vector<ElementType>::const_iterator it =
                 tmp.begin();
             it != tmp.end(); it++) {
            std::stringstream ss(*it);
            std::copy(std::istream_iterator<ElementType>(ss),
                std::istream_iterator<ElementType>(),
                std::back_inserter(var));
        }
        return true;
    }
    return false;
}

template <typename ValueType>
bool GetOptValueImpl(
    const boost::program_options::variables_map &var_map,
    ValueType &var, const std::string &val, ValueType*,
    bool if_not_defaulted) {
    // Check if the value is present.
    if (var_map.count(val) && (!if_not_defaulted ||
        (if_not_defaulted && !var_map[val].defaulted()))) {
        var = var_map[val].as<ValueType>();
        return true;
    }
    return false;
}

template <typename ValueType>
bool GetOptValue(const boost::program_options::variables_map &var_map,
                          ValueType &var, const std::string &val) {
    return GetOptValueImpl(var_map, var, val, static_cast<ValueType *>(0),
        false);
}

template <typename ValueType>
bool GetOptValueIfNotDefaulted(
    const boost::program_options::variables_map &var_map,
    ValueType &var, const std::string &val) {
    return GetOptValueImpl(var_map, var, val, static_cast<ValueType *>(0),
        true);
}

}  // namespace util
}  // namespace options

#endif  // BASE_OPTIONS_UTIL_H_
