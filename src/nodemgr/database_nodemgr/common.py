#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import subprocess


class CassandraManager(object):
    def __init__(self, cassandra_repair_logdir):
        self.cassandra_repair_logdir = cassandra_repair_logdir

    def status(self):
        subprocess.Popen(["contrail-cassandra-status",
                          "--log-file", "/var/log/cassandra/status.log",
                          "--debug"])

    def repair(self):
        logdir = self.cassandra_repair_logdir + "repair.log"
        subprocess.Popen(["contrail-cassandra-repair",
                          "--log-file", logdir,
                          "--debug"])
