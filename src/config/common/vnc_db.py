#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of database model for contrail config daemons
"""

class DBBase(object):
    # This is the base class for all DB objects. All derived objects must
    # have a class member called _dict of dictionary type.
    # The init method of this class must be callled before using any functions

    _logger = None
    _cassandra = None

    @classmethod
    def init(cls, logger, cassandra):
        cls._logger = logger
        cls._cassandra = cassandra
    # end init

    class __metaclass__(type):

        def __iter__(cls):
            for i in cls._dict:
                yield i
        # end __iter__

        def values(cls):
            for i in cls._dict.values():
                yield i
        # end values

        def items(cls):
            for i in cls._dict.items():
                yield i
        # end items
    # end __metaclass__

    @classmethod
    def get(cls, key):
        if key in cls._dict:
            return cls._dict[key]
        return None
    # end get

    @classmethod
    def locate(cls, key, *args):
        if key not in cls._dict:
            try:
                cls._dict[key] = cls(key, *args)
            except NoIdError as e:
                cls._logger.debug(
                    "Exception %s while creating %s for %s",
                    e, cls.__name__, key)
                return None
        return cls._dict[key]
    # end locate

    @classmethod
    def delete(cls, key):
        if key in cls._dict:
            del cls._dict[key]
    # end delete

# end class DBBase