#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# mockzoo
#
# This module helps start and stop IFMAP instances for unit testing
# java must be pre-installed for this to work
#
    
import os
import subprocess
import logging
import socket
from cfgm_common.ifmap.client import client, namespaces
from cfgm_common.ifmap.request import NewSessionRequest
import time

logging.basicConfig(level=logging.INFO,
                            format='%(asctime)s %(levelname)s %(message)s')
ifmap_version = '0.3.2'
ifmap_url = '/tmp/irond-'+ifmap_version+'-bin.zip'

def start_ifmap(cport1):
    '''
    Client uses this function to start an instance of IFMAP
    Arguments:
        cport : An unused TCP port for zookeeper to use as the client port
    '''
    ifmap_download = 'curl -o ' +\
        ifmap_url + ' -s -m 120 http://trust.f4.hs-hannover.de/download/iron/archive/irond-'+\
        ifmap_version+'-bin.zip'
    if not os.path.exists(ifmap_url):
        process = subprocess.Popen(ifmap_download.split(' '))
        process.wait()
        if process.returncode is not 0:
            return

    cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cs.bind(("",0))
    cport2 = cs.getsockname()[1]
    cs.close()

    basefile = "irond-"+ifmap_version+'-bin'
    zipfile = ifmap_url
    cassbase = "/tmp/irond." + str(cport1) + "/"
    confdir = cassbase + basefile + "/"
    output,_ = call_command_("mkdir " + cassbase)

    logging.info('Installing irond in ' + cassbase)
    os.system("unzip " + zipfile + " -d " + cassbase)

    logging.info('irond Client Ports %d , %d' % (cport1,cport2))

    conftemplate = os.path.dirname(os.path.abspath(__file__)) + "/ifmap.properties"
    output, _ = call_command_("cp " + conftemplate + " " + confdir)

    conftemplate = os.path.dirname(os.path.abspath(__file__)) + "/basicauthusers.properties"
    output, _ = call_command_("cp " + conftemplate + " " + confdir)

    conftemplate = os.path.dirname(os.path.abspath(__file__)) + "/publisher.properties"

    output, _ = call_command_("cp " + conftemplate + " " + confdir)
    
    replace_string_(confdir + "log4j.properties",
        [("TRACE","DEBUG")])

    replace_string_(confdir + "ifmap.properties", \
        [("irond.comm.basicauth.port=8443","irond.comm.basicauth.port="+str(cport1)),
         ("irond.comm.certauth.port=8444","irond.comm.certauth.port="+str(cport2)),
         ("irond.auth.basic.users.file=/etc/irond/basicauthusers.properties","irond.auth.basic.users.file=%sbasicauthusers.properties" % confdir),
         ("irond.auth.cert.keystore.file=/usr/share/irond/keystore/irond.jks","irond.auth.cert.keystore.file=%skeystore/irond.jks" % confdir),
         ("irond.ifmap.publishers.file=/etc/irond/publisher.properties","irond.ifmap.publishers.file=%spublisher.properties" % confdir),
         ("irond.ifmap.authorization.file=/etc/irond/authorization.properties","irond.ifmap.authorization.file=%sauthorization.properties" % confdir),
         ("irond.auth.cert.truststore.file=/usr/share/irond/keystore/irond.jks","irond.auth.cert.truststore.file=%skeystore/irond.jks")])
    replace_string_(confdir + "start.sh", \
         [("java -jar irond.jar","java -jar %sirond.jar" % confdir)])
    output, _ = call_command_("chmod +x %sstart.sh" % confdir)
   
    commd = confdir + "start.sh"
    jcommd = "java -jar %sirond.jar" % confdir
    #import pdb; pdb.set_trace()
    #subprocess.Popen(jcommd.split(' '), cwd=confdir,
    #                           stdout=subprocess.PIPE,
    #                           stderr=subprocess.PIPE)
    subprocess.Popen(jcommd.split(' '), cwd=confdir)
    
    ns = {
        'env':   "http://www.w3.org/2003/05/soap-envelope",
        'ifmap':   "http://www.trustedcomputinggroup.org/2010/IFMAP/2",
        'meta': "http://www.trustedcomputinggroup.org/2010/IFMAP-METADATA/2"}
    ifmap_srv_ip = "127.0.0.1"
    ifmap_srv_port = cport1
    uname = "test"
    passwd = "test"
    mapclient = client(("%s" % (ifmap_srv_ip), "%s" % (ifmap_srv_port)),
        uname, passwd, ns, None)
    connected = False
    while not connected:
        try:
            result = mapclient.call('newSession', NewSessionRequest())
        finally:
            if result != None:
                connected = True
            else:
                logging.info('Irond not started...')
                time.sleep(2)

    logging.info('Started Irond')

    #output,_ = call_command_(confdir + "start.sh", cwd = confdir, shell=True)

def stop_ifmap(cport1):
    '''
    Client uses this function to stop an instance of IFMAP
    Arguments:
        cport : The Client Port for the instance of IFMAP to be stopped
    '''
    basefile = "irond-"+ifmap_version+'-bin'
    cassbase = "/tmp/irond." + str(cport1) + "/"
    confdir = cassbase + basefile + "/"

    logging.info('Killing ifmap %d' % cport1)
    output,_ = call_command_("pkill -f %sirond.jar" % confdir)
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

def call_command_(command, cwd = None):
    process = subprocess.Popen(command.split(' '), cwd=cwd,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    return process.communicate()

if __name__ == "__main__":
    cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    cs.bind(("",0))
    cport = cs.getsockname()[1]
    cs.close()
    start_ifmap(cport)


