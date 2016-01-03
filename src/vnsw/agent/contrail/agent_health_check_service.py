#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import sys
from threading import Event
from subprocess import call
from optparse import OptionParser

class HealthCheckBase():
    def __init__(self, event, ip, timer, timeout):
        self.stopped = event
        self.ip = ip
        self.timer = timer
        self.timeout = timeout
        self.initial = True
        self.ret = 0
        self.dev_null_fd = open(os.devnull, 'w')

    def start(self):
        while (self.initial or (not self.stopped.wait(self.timer))):
            ret = self.execute_util(self.ip, self.timeout, self.dev_null_fd);
            if self.initial == True or ret != self.ret:
                self.initial = False
                self.ret = ret
                if self.ret == 0:
                    sys.stdout.write("Success\n")
                    sys.stdout.flush()
                else:
                    sys.stdout.write("Failed\n")
                    sys.stdout.flush()

    def execute_util(self, ip, timeout, out_fd):
        sys.stderr.write("Error: health check method not defined!\n")
        sys.exit(127)

class HealthCheckPing(HealthCheckBase):
    def execute_util(self, ip, timeout, out_fd):
        return call(["ping", "-c2", "-W" + str(timeout), ip], stdout=out_fd,
                    stderr=out_fd)

def HealthCheckService(x):
    return {
        "ping": HealthCheckPing,
        "Ping": HealthCheckPing,
        "PING": HealthCheckPing,
    }.get(x, HealthCheckBase) #[x]

parser = OptionParser()
parser.add_option("-m", "--method", dest="method",
                  help="method to do Health Check (Ping/Http)",
                  metavar="METHOD")
parser.add_option("-d", "--destip", dest="dest_ip",
                  help="Destination IP to run Health Check", metavar="IP",
                  default="127.0.0.1")
parser.add_option("-i", "--interval", dest="interval", type="int",
                  help="Health Check interval in seconds", metavar="TIME",
                  default="2")
parser.add_option("-t", "--timeout", dest="timeout", type="int",
                  help="Health Check timeout in seconds", metavar="TIME",
                  default="1")
(option, args) = parser.parse_args()
if option.interval < 2:
    option.interval = 2
if option.timeout < 1:
    option.timeout = 1

stopFlag = Event()
service = HealthCheckService(option.method)(stopFlag, option.dest_ip,
                                            option.interval, option.timeout)
service.start()

