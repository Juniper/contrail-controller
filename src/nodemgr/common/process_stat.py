#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import socket


class ProcessStat(object):
    def __init__(self, pname, host_ip, last_cpu=None, last_time=0, hostname=None):
        self.start_count = 0
        self.stop_count = 0
        self.exit_count = 0
        self.start_time = ''
        self.exit_time = ''
        self.stop_time = ''
        self.core_file_list = []
        self.last_exit_unexpected = False
        self.deleted = False
        self.process_state = 'PROCESS_STATE_STOPPED'
        self.group = 'default'
        self.name = socket.getfqdn(host_ip) if hostname is None \
            else hostname
        self.pname = pname
        self.pid = 0
        self.last_cpu = last_cpu
        self.last_time = last_time
