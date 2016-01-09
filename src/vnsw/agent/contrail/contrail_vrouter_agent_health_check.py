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
    def __init__(self, event, ip, timer, timeout, retries, uri):
        self.event = event
        self.ip = ip
        self.timer = timer
        self.timeout = timeout
        self.retries = retries
        self.uri = uri;
        self.retries_done = 0
        self.initial = True
        self.ret = 0
        self.stop = False
        self.dev_null_fd = open(os.devnull, 'w')

    def start(self):
        while (self.initial or (not self.event.wait(self.timer))):
            if (self.stop == False):
                ret = self.execute_util(self.ip, self.timeout, self.uri,
                                        self.dev_null_fd)
            self.retries_done += 1
            if ret != 0 and self.retries_done != self.retries:
                # ignore by restoring previous response,
                # till number of retries are completed
                ret = self.ret
            else:
                self.retries_done = 0
                # TODO(prabhjot) need to add log for every transaction
                if self.initial == True or ret != self.ret:
                    # TODO(prabhjot) currently script uses failure to
                    # write to stdout as a mechanism to identify parent
                    # process close and exit, eventually we should
                    # restore below line to optimize unneccessary message
                    # exchange on the stdout pipe
                    # self.initial = False
                    self.ret = ret
                    if self.ret == 0:
                        sys.stdout.write("Success\n")
                        sys.stdout.flush()
                    else:
                        sys.stdout.write("Failure\n")
                        sys.stdout.flush()

    def execute_util(self, ip, timeout, uri, out_fd):
        sys.stderr.write("Error: health check method not defined!\n")
        sys.stderr.flush()
        # pause script and stop retrying
        self.stop = True
        self.retries = 1
        return 1

class HealthCheckPing(HealthCheckBase):
    def execute_util(self, ip, timeout, uri, out_fd):
        return call(["ping", "-c2", "-W" + str(timeout), ip], stdout=out_fd,
                    stderr=out_fd)

class HealthCheckHttp(HealthCheckBase):
    def execute_util(self, ip, timeout, uri, out_fd):
        return call(["curl", "-m" + str(timeout), "http://" + ip + "/" + uri],
                    stdout=out_fd, stderr=out_fd)

def HealthCheckService(x):
    return {
        "ping": HealthCheckPing,
        "Ping": HealthCheckPing,
        "PING": HealthCheckPing,
        "http": HealthCheckHttp,
        "Http": HealthCheckHttp,
        "HTTP": HealthCheckHttp,
    }.get(x, HealthCheckBase) #[x]

parser = OptionParser()
parser.add_option("-m", "--method", dest="method",
                  help="method to do Health Check (Ping/Http)",
                  metavar="METHOD")
parser.add_option("-d", "--destip", dest="dest_ip",
                  help="Destination IP to run Health Check", metavar="IP",
                  default="")
parser.add_option("-i", "--interval", dest="interval", type="int",
                  help="Health Check interval in seconds", metavar="TIME",
                  default="2")
parser.add_option("-t", "--timeout", dest="timeout", type="int",
                  help="Health Check timeout in seconds", metavar="TIME",
                  default="1")
parser.add_option("-r", "--retries", dest="retries", type="int",
                  help="Number of retries to be done to declare error",
                  metavar="COUNT", default="2")
parser.add_option("-u", "--uri", dest="uri",
                  help="Additional indentifier for Health Check object",
                  metavar="STRING", default="")
(option, args) = parser.parse_args()

if option.interval < 1:
    option.interval = 1

if option.timeout < 1:
    option.timeout = 1

if option.retries < 1:
    option.retries = 1

timerEvent = Event()
service = HealthCheckService(option.method)(timerEvent, option.dest_ip,
                                            option.interval, option.timeout,
                                            option.retries, option.uri)
service.start()

