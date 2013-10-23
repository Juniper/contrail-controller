#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# mockzoo
#
# This module helps start and stop zookeeper instances for unit testing
# java must be pre-installed for this to work
#
    
import os
import subprocess
import logging
import socket
from kazoo.client import KazooClient

logging.basicConfig(level=logging.INFO,
                            format='%(asctime)s %(levelname)s %(message)s')

def start_zoo(cport):
    '''
    Client uses this function to start an instance of zookeeper
    Arguments:
        cport : An unused TCP port for zookeeper to use as the client port
    '''
    basefile = "zookeeper-3.4.5"
    tarfile = os.path.dirname(os.path.abspath(__file__)) + "/" + basefile + ".tar.gz"
    cassbase = "/tmp/zoo." + str(cport) + "/"
    confdir = cassbase + basefile + "/conf/"
    output,_ = call_command_("mkdir " + cassbase)

    logging.info('Installing zookeeper in ' + cassbase + " conf " + confdir)
    os.system("cat " + tarfile + " | tar -xpzf - -C " + cassbase)

    output,_ = call_command_("cp " + confdir + "zoo_sample.cfg " + confdir + "zoo.cfg")

    logging.info('zookeeper Client Port %d' % cport)

    replace_string_(confdir + "zoo.cfg", \
        [("dataDir=/tmp/zookeeper", "dataDir="+cassbase)])

    replace_string_(confdir + "zoo.cfg", \
        [("clientPort=2181", "clientPort="+str(cport))])

    output,_ = call_command_(cassbase + basefile + "/bin/zkServer.sh start")

    zk = KazooClient(hosts='127.0.0.1:'+str(cport))
    zk.start()
    zk.stop()

def stop_zoo(cport):
    '''
    Client uses this function to stop an instance of zookeeper
    This will only work for zookeeper instances that were started by this module
    Arguments:
        cport : The Client Port for the instance of zookeper be stopped
    '''
    cassbase = "/tmp/zoo." + str(cport) + "/"
    input = open(cassbase + "zookeeper_server.pid")
    s=input.read()
    logging.info('Killing zookeeper pid %d' % int(s))
    output,_ = call_command_("kill -9 %d" % int(s))
    output,_ = call_command_("rm -rf " + cassbase)
    
def replace_string_(filePath, findreplace):
    "replaces all findStr by repStr in file filePath"
    print filePath
    tempName=filePath+'~~~'
    input = open(filePath)
    output = open(tempName,'w')
    s=input.read()
    for couple in findreplace:
        outtext=s.replace(couple[0],couple[1])
        s=outtext
    output.write(outtext)
    output.close()
    input.close()
    os.rename(tempName,filePath)

def call_command_(command):
    process = subprocess.Popen(command.split(' '),
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    return process.communicate()

if __name__ == "__main__":
    cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cs.bind(("",0))
    cport = cs.getsockname()[1]
    cs.close()
    start_zoo(cport)


