#!/usr/bin/env python

#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

#
# mockkafka
#
# This module helps start and stop kafka instances for unit testing
# It uses mock zookeeper provided by config 
#
    
import os
import subprocess
import logging
import socket
import platform
import time
from kafka import KafkaClient

logging.basicConfig(level=logging.INFO,
                            format='%(asctime)s %(levelname)s %(message)s')

kafka_version = '2.9.2-0.8.1.1'
kafka_bdir  = '/tmp/cache/' + os.environ['USER'] + '/systemless_test'
kafka_src = ''

def start_kafka(zk_client_port, broker_listen_port, broker_id=0):
    if not os.path.exists(kafka_bdir):
        output,_ = call_command_("mkdir " + kafka_bdir)
    kafka_download = 'wget -P ' + kafka_bdir + ' http://download.nextag.com/apache/kafka/0.8.1.1/kafka_2.9.2-0.8.1.1.tgz'

    if not os.path.exists(kafka_bdir+'/kafka_2.9.2-0.8.1.1.tgz'):
        process = subprocess.Popen(kafka_download.split(' '))
        process.wait()
        if process.returncode is not 0:
            return False

    basefile = 'kafka_2.9.2-0.8.1.1'
    kafkabase = "/tmp/kafka.%s.%d/" % (os.getenv('USER', 'None'), broker_listen_port)
    confdir = kafkabase + basefile + "/config/"
    output,_ = call_command_("rm -rf " + kafkabase)
    output,_ = call_command_("mkdir " + kafkabase)

    logging.info('Installing kafka in ' + kafkabase)
    os.system("cat " + kafka_bdir+'/kafka_2.9.2-0.8.1.1.tgz' + " | tar -xpzf - -C " + kafkabase)

    logging.info('kafka Port %d' % broker_listen_port)
 
    #Replace the brokerid and port # in the config file
    replace_string_(confdir+"server.properties",
        [("broker.id=0","broker.id="+str(broker_id)),
         ("port=9092","port="+str(broker_listen_port)),
         ("zookeeper.connect=localhost:2181", "zookeeper.connect=localhost:%d" % zk_client_port),
         ("log.dirs=/tmp/kafka-logs","log.dirs="+kafkabase+"logs")])

    replace_string_(kafkabase + basefile + "/bin/kafka-server-stop.sh",
        [("grep -v grep", "grep %d | grep -v grep" % broker_listen_port)])
    replace_string_(kafkabase + basefile + "/bin/kafka-server-stop.sh",
        [("SIGINT", "SIGKILL")])
    replace_string_(kafkabase + basefile + "/bin/kafka-server-stop.sh",
        [("#!/bin/sh", "#!/bin/sh -x")])
    output,_ = call_command_("chmod +x " + kafkabase + basefile + "/bin/kafka-server-stop.sh") 

    output,_ = call_command_(kafkabase + basefile + "/bin/kafka-server-start.sh -daemon " + kafkabase + basefile + "/config/server.properties")

    count = 0
    while count < 15:
        try:
            logging.info('Trying to connect...')
            kk = KafkaClient("localhost:%d" % broker_listen_port)
        except:
            count += 1
            time.sleep(1)
        else:
            return True

    logging.info("Kafka client cannot connect. Kafka logfile below:")
    with open(kafkabase + basefile + "/logs/kafkaServer.out", 'r') as fin:
        logging.info(fin.read())
    return False

def stop_kafka(broker_listen_port):
    '''
    Client uses this function to stop an instance of kafka
    This will only work for kafka instances that were started by this module
    Arguments:
        cport : The Kafka Port for the instance of cassandra to be stopped
    '''
    kafkabase = "/tmp/kafka.%s.%d/" % (os.getenv('USER', 'None'), broker_listen_port)
    basefile = 'kafka_2.9.2-0.8.1.1'
    logging.info('Killing kafka in %s' % (kafkabase + basefile))
    output,_ = call_command_(kafkabase + basefile + "/bin/kafka-server-stop.sh")

    logging.info('Killed kafka for %d' % broker_listen_port)
    output,_ = call_command_("rm -rf " + kafkabase)

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

    distribution = platform.dist()[0]
    if distribution == "debian":
        jenv = { "JAVA_HOME" : "/usr/local/java/jre1.6.0_43" }
    else:
        jenv = None

    process = subprocess.Popen(command.split(' '), env = jenv,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    return process.communicate()

if __name__ == "__main__":
    zk_client_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    zk_client_sock.bind(("",0))
    zk_client_port = zk_client_sock.getsockname()[1]
    broker_listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    broker_listen_sock.bind(("",0))
    broker_listen_port = broker_listen_sock.getsockname()[1]
    zk_client_sock.close()
    broker_listen_sock.close()

    from mockzoo import mockzoo
    mockzoo.start_zoo(zk_client_port)
    start_kafka(zk_client_port, broker_listen_port)

