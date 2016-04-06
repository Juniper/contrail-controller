# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
from __future__ import unicode_literals
from distutils.util import strtobool
import logging
import sys
import time


logger = logging.getLogger(__name__)


def prompt(query):
    sys.stdout.write('%s [y/n]: ' % query)
    val = raw_input()
    try:
        ret = strtobool(val.lower())
    except ValueError:
        sys.stdout.write('Please answer with a y/n\n')
        return prompt(query)
    return ret


def camel_case(string):
    string = string.replace('-', '_')
    return ''.join([word.capitalize() for word in string.split('_')])


def timeit(return_time_elapsed=False):
    def timed(method):
        def wrapper(*args, **kwargs):
            time_start = time.time()
            result = method(*args, **kwargs)
            time_elapsed = time.time() - time_start
            logger.debug('%r (%r, %r) %2.2f sec',
                         method.__name__, args, kwargs, time_elapsed)
            if return_time_elapsed:
                return (result, time_elapsed)
            else:
                return result
        return wrapper
    return timed
