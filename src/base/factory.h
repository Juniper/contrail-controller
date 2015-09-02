/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE__FACTORY_H__
#define __BASE__FACTORY_H__

#include <boost/function.hpp>
#include <boost/functional/factory.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/utility/enable_if.hpp>

#include "base/util.h"

template <class Derived>
class Factory {
  protected:
    static Derived *GetInstance() {
        if (singleton_ == NULL) {
            singleton_ = new Derived();
        }
        return singleton_;
    }
  private:
    static Derived *singleton_;
};

#include "base/factory_macros.h"

#define FACTORY_STATIC_REGISTER(_Factory, _BaseType, _TypeImpl)\
static void _Factory ## _TypeImpl ## Register () {\
    _Factory::Register<_BaseType>(boost::factory<_TypeImpl *>());\
}\
MODULE_INITIALIZER(_Factory ## _TypeImpl ## Register)

#endif
