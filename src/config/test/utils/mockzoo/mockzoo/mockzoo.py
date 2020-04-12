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
zookeeper_version = 'zookeeper-3.4.5'
zookeeper_dl = '/zookeeper-3.4.5.tar.gz'
zookeeper_bdir  = '/tmp/cache-' + os.environ['USER'] + '-systemless_test'

def start_zoo(cport):
    '''
    Client uses this function to start an instance of zookeeper
    Arguments:
        cport : An unused TCP port for zookeeper to use as the client port
    '''
    if not os.path.exists(zookeeper_bdir):
        output,_ = call_command_("mkdir " + zookeeper_bdir)
    zookeeper_download = 'wget -O ' + zookeeper_bdir + zookeeper_dl + \
        ' https://github.com/Juniper/contrail-third-party-cache/blob/master/zookeeper' + \
        zookeeper_dl + '?raw=true'

    if not os.path.exists(zookeeper_bdir + zookeeper_dl):
        process = subprocess.Popen(zookeeper_download.split(' '))
        process.wait()
        if process.returncode is not 0:
            logging.info('Cannot download Zk: %s' % zookeeper_download)
            return False

    basefile = zookeeper_version
    cassbase = "/tmp/zoo.%s.%d/" % (os.getenv('USER', 'None'), cport)
    confdir = cassbase + basefile + "/conf/"
    output,_ = call_command_("mkdir " + cassbase)

    logging.info('Installing zookeeper in ' + cassbase) 
    os.system("cat " + zookeeper_bdir + zookeeper_dl + " | tar -xpzf - -C " + cassbase)

    output,_ = call_command_("cp " + confdir + "zoo_sample.cfg " + confdir + "zoo.cfg")

    logging.info('zookeeper Client Port %d' % cport)

    replace_string_(confdir + "zoo.cfg", \
        [("dataDir=/tmp/zookeeper", "dataDir="+cassbase),
         ("clientPort=2181", "clientPort="+str(cport))])

    replace_string_(cassbase + basefile + "/bin/zkServer.sh", \
        [('_ZOO_DAEMON_OUT="$ZOO_LOG_DIR/', '_ZOO_DAEMON_OUT="%s' % cassbase)])
    output,_ = call_command_("chmod +x " + cassbase + basefile + "/bin/zkServer.sh")
    output,_ = call_command_(cassbase + basefile + "/bin/zkServer.sh start")

    zk = KazooClient(hosts='127.0.0.1:'+str(cport))
    try:
        zk.start()
    except:
        logging.info("Zookeeper client cannot connect. Zk logfile below:")
        with open(cassbase+"zookeeper.out", 'r') as fin:
            logging.info(fin.read())
        return False
    zk.stop()
    return True

def stop_zoo(cport):
    '''
    Client uses this function to stop an instance of zookeeper
    This will only work for zookeeper instances that were started by this module
    Arguments:
        cport : The Client Port for the instance of zookeper be stopped
    '''
    cassbase = "/tmp/zoo.%s.%d/" % (os.getenv('USER', 'None'), cport)
    basefile = zookeeper_version
    logging.info('Killing zookeeper %d' % cport)
    output,_ = call_command_(cassbase + basefile + '/bin/zkServer.sh stop')
    output,_ = call_command_("rm -rf " + cassbase)
    
def replace_string_(filePath, findreplace):
    "replaces all findStr by repStr in file filePath"
    print(filePath)
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


