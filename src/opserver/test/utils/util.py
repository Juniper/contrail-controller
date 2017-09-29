#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import math
import subprocess
import os
import time
import socket

# Code borrowed from http://wiki.python.org/moin/PythonDecoratorLibrary#Retry


def retry(tries=5, delay=3):
    '''Retries a function or method until it returns True.
    delay sets the initial delay in seconds.
    '''
    tries = tries * 1.0
    tries = math.floor(tries)
    if tries < 0:
        raise ValueError("tries must be 0 or greater")

    if delay <= 0:
        raise ValueError("delay must be greater than 0")

    def deco_retry(f):
        def f_retry(*args, **kwargs):
            mtries, mdelay = tries, delay  # make mutable

            rv = f(*args, **kwargs)  # first attempt
            while mtries > 0:
                if rv is True:  # Done on success
                    return True
                mtries -= 1      # consume an attempt
                time.sleep(mdelay)  # wait...

                rv = f(*args, **kwargs)  # Try again
            return False  # Ran out of tries :-(

        return f_retry  # true decorator -> decorated function
    return deco_retry  # @retry(arg[, ...]) -> true decorator
# end retry


def web_invoke(httplink):
    cmd = 'curl ' + httplink
    output = None
    try:
        output = subprocess.check_output(cmd, shell=True)
    except Exception:
        output = None
    return output
# end web_invoke


def obj_to_dict(obj):
    if isinstance(obj, dict):
        data = {}
        for k, v in obj.iteritems():
            data[k] = obj_to_dict(v)
        return data
    elif hasattr(obj, '__iter__'):
        return [obj_to_dict(v) for v in obj]
    elif hasattr(obj, '__dict__'):
        data = dict([(key, obj_to_dict(value)) 
            for key, value in obj.__dict__.iteritems() 
            if value is not None and not callable(value) and \
               not key.startswith('_')])
        return data
    else:
        return obj
# end obj_to_dict

def find_buildroot(path):
    try:
        return os.environ['BUILDTOP']
    except:
        return path + '/build/debug'
#end find_buildroot

def get_free_port():
    cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cs.bind(("", 0))
    cport = cs.getsockname()[1]
    cs.close()
    return cport
#end get_free_port
