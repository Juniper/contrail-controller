#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# mockredis
#
# This module helps start and stop redis instances for unit-testing
# redis must be pre-installed for this to work
#

import os
import signal
import subprocess
import logging
import socket
import time
import redis

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')


def redis_version():
    '''
    Determine redis-server version
    '''
    command = "redis-server --version"
    process = subprocess.Popen(command.split(' '), stdout=subprocess.PIPE)
    output = process.communicate()
    exit_code = process.wait()
    if "v=2.6" in output[0]:
        return 2.6
    else:
        return 2.4


def start_redis(port, master_port=None):
    '''
    Client uses this function to start an instance of redis
    Arguments:
        cport : An unused TCP port for redis to use as the client port
    '''
    version = redis_version()
    if version == 2.6:
        redis_conf = "redis.26.conf"
    else:
        redis_conf = "redis.24.conf"

    conftemplate = os.path.dirname(os.path.abspath(__file__)) + "/" +\
        redis_conf
    redisbase = "/tmp/redis." + str(port) + "/"
    output, _ = call_command_("rm -rf " + redisbase)
    output, _ = call_command_("mkdir " + redisbase)
    output, _ = call_command_("mkdir " + redisbase + "cache")
    logging.info('Redis Port %d' % port)

    output, _ = call_command_("cp " + conftemplate + " " + redisbase +
                              redis_conf)
    replace_string_(redisbase + redis_conf,
                    [("/var/run/redis_6379.pid", redisbase + "pid"),
                     ("port 6379", "port " + str(port)),
                     ("/var/log/redis_6379.log", redisbase + "log"),
                     ("/var/lib/redis/6379", redisbase + "cache")])
    if master_port is not None:
        replace_string_(redisbase + redis_conf,
                        [("# slaveof <masterip> <masterport>", 
                          "slaveof 127.0.0.1 " + str(master_port))])
    output, _ = call_command_("redis-server " + redisbase + redis_conf)
    r = redis.StrictRedis(host='localhost', port=port, db=0)
    done = False
    while not done:
        try:
            r.ping()
        except:
            logging.info('Redis not ready')
            time.sleep(1)
        else:
            done = True
    logging.info('Redis ready')


def stop_redis(port):
    '''
    Client uses this function to stop an instance of redis
    This will only work for redis instances that were started by this module
    Arguments:
        cport : The Client Port for the instance of redis to be stopped
    '''
    r = redis.StrictRedis(host='localhost', port=port, db=0)
    r.shutdown()
    del r
    redisbase = "/tmp/redis." + str(port) + "/"
    '''
    pidfile  = redisbase + "pid"
    pid = int(open(pidfile).read())
    os.kill(pid, signal.SIGTERM)
    '''
    output, _ = call_command_("rm -rf " + redisbase)


def start_redis_sentinel(port, redis_port):
    '''
    Client uses this function to start an instance of redis sentinel
    Arguments:
        port : An unused TCP port for redis sentinel
        redis_port : redis master port
    '''
    sentinel_conf_tmpl = os.path.dirname(os.path.abspath(__file__)) +\
        "/sentinel.conf"
    logging.info("Redis Sentinel Port %d" % port)
    sentinel_conf = "/tmp/sentinel_" + str(port) + ".conf"
    output, _ = call_command_("rm -f " + sentinel_conf)
    output, _ = call_command_("cp " + sentinel_conf_tmpl + " " + sentinel_conf)
    replace_string_(sentinel_conf,
                    [("/var/run/sentinel_26379.pid", "/tmp/sentinel_" +
                      str(port) + ".pid"),
                     ("port 26379", "port " + str(port)),
                     ("sentinel monitor query 127.0.0.1 6380 1",
                      "sentinel monitor query 127.0.0.1 " + str(redis_port) +
                      " 1"),
                     ("sentinel monitor uvelocal 127.0.0.1 6381 1",
                      "sentinel monitor uvelocal 127.0.0.1 " + str(redis_port) +
                      " 1"),
                     ("sentinel monitor mymaster 127.0.0.1 6381 1",
                      "sentinel monitor mymaster 127.0.0.1 " + str(redis_port) +
                      " 1")])
    output, _ = call_command_("redis-server " + sentinel_conf + " --sentinel")
    sentinel = redis.StrictRedis(host='localhost', port=port, db=0)
    done = False
    while not done:
        try:
            sentinel.ping()
        except:
            logging.info('Redis Sentinel not ready')
            time.sleep(1)
        else:
            done = True
    logging.info('Redis Sentinel ready')


def stop_redis_sentinel(port):
    '''
    Client uses this function to stop an instance of redis sentinel
    Arguments:
        port : The Client Port for the instance of redis sentinel to be stopped
    '''
    pidfile = "/tmp/sentinel_" + str(port) + ".pid"
    pid = int(open(pidfile).read())
    os.kill(pid, signal.SIGTERM)
    output, _ = call_command_("rm -f " + pidfile)
    sentinel_conf = "/tmp/sentinel_" + str(port) + ".conf"
    output, _ = call_command_("rm -f " + sentinel_conf)


def replace_string_(filePath, findreplace):
    "replaces all findStr by repStr in file filePath"
    print filePath
    tempName = filePath + '~~~'
    input = open(filePath)
    output = open(tempName, 'w')
    s = input.read()
    for couple in findreplace:
        outtext = s.replace(couple[0], couple[1])
        s = outtext
    output.write(outtext)
    output.close()
    input.close()
    os.rename(tempName, filePath)


def call_command_(command):
    process = subprocess.Popen(command.split(' '),
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    return process.communicate()


if __name__ == "__main__":
    cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cs.bind(("", 0))
    cport = cs.getsockname()[1]
    cs.close()
    start_redis(cport)
