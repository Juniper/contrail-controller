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
import os.path
import subprocess
import logging
import socket
import platform
import time
from kafka import KafkaClient
from kazoo.client import KazooClient

logging.basicConfig(level=logging.INFO,
                            format='%(asctime)s %(levelname)s %(message)s')
kafka_version = 'kafka_2.11-1.1.1-1'
kafka_dl = '/kafka_2.11-1.1.1-1.tar.gz'
kafka_bdir  = '/tmp/cache-' + os.environ['USER'] + '-systemless_test'

def start_kafka(zk_client_port, broker_listen_port, broker_id=0):
    if not os.path.exists(kafka_bdir):
        output,_ = call_command_("mkdir " + kafka_bdir)
    kafka_download = 'wget -O ' + kafka_bdir + kafka_dl + \
        ' https://github.com/Juniper/contrail-third-party-cache/blob/master/kafka' + \
        kafka_dl + '?raw=true'
    if not os.path.exists(kafka_bdir + kafka_dl):
        process = subprocess.Popen(kafka_download.split(' '))
        process.wait()
        if process.returncode is not 0:
            return False

    basefile = kafka_version
    kafkabase = "/tmp/kafka.%s.%d/" % (os.getenv('USER', 'None'), broker_listen_port)
    confdir = kafkabase + basefile + "/etc/kafka/"
    output,_ = call_command_("rm -rf " + kafkabase)
    output,_ = call_command_("mkdir " + kafkabase)

    logging.info('Check zookeeper in %d' % zk_client_port)
    zk = KazooClient(hosts='127.0.0.1:'+str(zk_client_port), timeout=60.0)
    try:
        zk.start()
        zk.delete("/brokers", recursive=True) 
        zk.delete("/consumers", recursive=True) 
        zk.delete("/controller", recursive=True) 
    except:
        logging.info("Zookeeper client cannot connect")
        zk.stop()
        return False
    zk.stop()
    logging.info('Installing kafka in ' + kafkabase)
    os.system("cat " + kafka_bdir + kafka_dl + " | tar -xpzf - -C " + kafkabase)

    logging.info('kafka Port %d' % broker_listen_port)
 
    replace_string_(confdir+"server.properties",
        [("#listeners=PLAINTEXT://:9092", \
          "port=9092\noffsets.topic.replication.factor=1")])

    #Replace the brokerid and port # in the config file
    replace_string_(confdir+"server.properties",
        [("broker.id=0","broker.id="+str(broker_id)),
         ("port=9092","port="+str(broker_listen_port)),
         ("zookeeper.connect=localhost:2181", "zookeeper.connect=localhost:%d" % zk_client_port),
         ("log.dirs=/var/lib/kafka","log.dirs="+kafkabase+basefile+"/logs")])

    replace_string_(kafkabase + basefile + "/usr/bin/kafka-server-start",
        [("base_dir=$(dirname $0)", \
          "base_dir=$(dirname $0)\n"+"LOG_DIR=\""+ \
           kafkabase+basefile+"/logs"+"\"\n"+"export LOG_DIR")])
    replace_string_(kafkabase + basefile + "/usr/bin/kafka-server-start",
        [("LOG4J_CONFIG_ZIP_INSTALL=\"$base_dir/../etc/kafka/log4j.properties\"", \
          "LOG4J_CONFIG_ZIP_INSTALL=\""+confdir+"/log4j.properties\"")])
    output,_ = call_command_("chmod +x " + kafkabase + basefile + \
            "/usr/bin/kafka-server-start") 

    replace_string_(kafkabase + basefile + "/usr/bin/kafka-server-stop",
        [("grep -v grep", "grep %s | grep -v grep" % kafkabase)])
    replace_string_(kafkabase + basefile + "/usr/bin/kafka-server-stop",
        [("SIGINT", "SIGKILL")])
    replace_string_(kafkabase + basefile + "/usr/bin/kafka-server-stop",
        [("#!/bin/sh", "#!/bin/sh -x")])
    output,_ = call_command_("chmod +x " + kafkabase + basefile + "/usr/bin/kafka-server-stop") 

    logging.info('starting kafka broker with call_command /usr/bin/kafka-server-start') 
    # Extra options for JMX : -Djava.net.preferIPv4Stack=true -Djava.rmi.server.hostname=xx.xx.xx.xx
    output,_ = call_command_(kafkabase + basefile + "/usr/bin/kafka-server-start -daemon " + confdir+"server.properties")

    count = 0
    start_wait = os.getenv('CONTRIAL_ANALYTICS_TEST_MAX_START_WAIT_TIME', 15)
    logging.info('Kafka broker started... cmd: %s'  % \
            (kafkabase + basefile + "/usr/bin/kafka-server-start -daemon " + \
            confdir+"server.properties"))
    while count < start_wait:
        try:
            logging.info('Trying to connect...')
            kk = KafkaClient("localhost:%d" % broker_listen_port)
        except:
            count += 1
            time.sleep(1)
        else:
            logging.info('connect to kafka success')
            with open(kafkabase + basefile + "/logs/kafkaServer.out", 'r') as fin:
                logging.info(fin.read())
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
    basefile = kafka_version
    logging.info('Killing kafka in %s' % (kafkabase + basefile))
    output,_ = call_command_(kafkabase + basefile + "/usr/bin/kafka-server-stop")

    count = 0
    start_wait = os.getenv('CONTRIAL_ANALYTICS_TEST_MAX_START_WAIT_TIME', 15)
    while count < start_wait:
        try:
            logging.info('Trying to connect...')
            kk = KafkaClient("localhost:%d" % broker_listen_port)
        except:
            break
        else:
            count += 1
            time.sleep(1)

    logging.info('Killed kafka for %d : server.log' % broker_listen_port)
    with open(kafkabase + basefile + "/logs/server.log", 'r') as fin:
        logging.info(fin.read())
    logging.info('Killed kafka for %d : controller.log' % broker_listen_port)
    with open(kafkabase + basefile + "/logs/controller.log", 'r') as fin:
        logging.info(fin.read())
    logging.info('Killed kafka for %d : kafkaServer.out' % broker_listen_port)
    with open(kafkabase + basefile + "/logs/kafkaServer.out", 'r') as fin:
        logging.info(fin.read())
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
    jpath = "/usr/local/java/jre1.6.0_43"
    if distribution == "debian" and os.path.isdir(jpath):
        jenv = { "JAVA_HOME" : jpath }
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

