#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import time
import paramiko
import logging
import ast
from cfgm_common.vnc_kombu import VncKombuClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from issu_contrail_common import ICCassandraClient
from issu_contrail_common import ICCassandraInfo
import issu_contrail_config


class ICAmqpInfo():
    def __init__(self, amqp_ip, amqp_port,
                 amqp_user, amqp_pwd, amqp_vhost,
                 amqp_ha, amqp_q):
        self.amqp_ip = amqp_ip
        self.amqp_port = amqp_port
        self.amqp_user = amqp_user
        self.amqp_pwd = amqp_pwd
        self.amqp_vhost = amqp_vhost
        self.amqp_ha = amqp_ha
        self.amqp_q = amqp_q


class ICKombuClient(VncKombuClient):
    def __init__(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger,
                 cassandra_inst, keyspace, new_api_info):

        self._cassandra_inst = cassandra_inst
        self._keyspace = keyspace
        self._new_api_info = new_api_info
        self.logger = logger
        super(ICKombuClient, self).__init__(rabbit_ip, rabbit_port,
                                            rabbit_user, rabbit_password,
                                            rabbit_vhost, rabbit_ha_mode,
                                            q_name, subscribe_cb, logger)

    def _act_on_api(self, action):
        cmd = '%s %s' % ("service supervisor-config", action)
        self.logger(cmd, level=SandeshLevel.SYS_INFO)
        for addr, clist in self._new_api_info.items():
            ssh = paramiko.SSHClient()
            ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            ssh.connect(addr, username=clist[0], password=clist[1])
            ssh_stdin, ssh_stdout, ssh_stderr = ssh.exec_command(cmd)
            exit_status = ssh_stdout.channel.recv_exit_status()
            self.logger(exit_status, level=SandeshLevel.SYS_INFO)
            ssh_cmd_dict = ssh_stdout.readlines()
            ssh_cmd_err = ssh_stderr.readlines()
            self.logger(ssh_cmd_dict, level=SandeshLevel.SYS_INFO)
            self.logger(ssh_cmd_err, level=SandeshLevel.SYS_INFO)
            ssh.close()

    def prepare_to_consume(self):
        self._act_on_api("stop")
        try:
            self.logger("Config sync initiated...",
                        level=SandeshLevel.SYS_INFO)
            print "Config Sync initiated..."
            self._cassandra_inst.issu_merge_copy(self._keyspace)
            self.logger("Config sync done...", level=SandeshLevel.SYS_INFO)
            print "Config Sync done..."
        except Exception as e:
            self.logger(e, level=SandeshLevel.SYS_INFO)
        self.logger("Started runtime sync...", level=SandeshLevel.SYS_INFO)
        print "Started runtime sync..."
        self._act_on_api("start")
        self.logger("Start Compute upgrade...", level=SandeshLevel.SYS_INFO)
        print "Start Compute upgrade..."


class ICRMQMain():
    def __init__(self, oldv_cass_info, newv_cass_info, old_rabbit_info,
                 new_rabbit_info, new_api_info, logger, rmq_issu_handler=None):
        self.old_cassandra = oldv_cass_info
        self.new_cassandra = newv_cass_info
        self.old_rabbit = old_rabbit_info
        self.new_rabbit = new_rabbit_info
        self.cassandra_issu_info = oldv_cass_info.issu_info
        self.logger = logger
        self.keyspace_info = oldv_cass_info.keyspace_info
        self.rmq_issu_handler = rmq_issu_handler
        self.new_api_info = new_api_info
        self.start()

    def amqp_callback_handler(self, msg):
        # Log it for debugging.
        self.logger(msg,  level=SandeshLevel.SYS_INFO)

    def issu_amqp_callback_handler(self, msg):
        self.logger(msg,  level=SandeshLevel.SYS_INFO)
        if self.rmq_issu_handler is not None:
            msg = self.rmq_issu_handler(msg)
        self.issu_cass_config_db_uuid_handle.issu_sync_row(msg,
                                                           'obj_uuid_table')
        self.amqp_new_version_handle.publish(msg)

    def start(self):
        # Create an instance of issu casandra_config_db_uuid
        self.issu_cass_config_db_uuid_handle = ICCassandraClient(
            self.old_cassandra.addr_info, self.new_cassandra.addr_info,
            self.old_cassandra.db_prefix, self.new_cassandra.db_prefix,
            self.cassandra_issu_info, self.logger)
        # Prepare it for Issu

        # Establish a amqp connection with newerversion
        self.amqp_new_version_handle = VncKombuClient(
            self.new_rabbit.amqp_ip, self.new_rabbit.amqp_port,
            self.new_rabbit.amqp_user, self.new_rabbit.amqp_pwd,
            self.new_rabbit.amqp_vhost, self.new_rabbit.amqp_ha,
            self.new_rabbit.amqp_q, self.amqp_callback_handler, self.logger)

        # Create a amqp connection with oldversion, passing all the information
        self.amqp_old_version_handle = ICKombuClient(
            self.old_rabbit.amqp_ip, self.old_rabbit.amqp_port,
            self.old_rabbit.amqp_user, self.old_rabbit.amqp_pwd,
            self.old_rabbit.amqp_vhost, self.old_rabbit.amqp_ha,
            self.old_rabbit.amqp_q, self.issu_amqp_callback_handler,
            self.logger, self.issu_cass_config_db_uuid_handle,
            self.keyspace_info, self.new_api_info)
    # end
# end issu_main


def _issu_rmq_main():
    # Create Instance of cassandra info
    logging.basicConfig(level=logging.INFO,
                        filename='/var/log/issu_contrail_run_sync.log',
                        format='%(asctime)s %(message)s')
    args, remaining_args = issu_contrail_config.parse_args()
    new_cassandra_info = ICCassandraInfo(
        args.new_cassandra_address_list,
        args.ndb_prefix,
        issu_contrail_config.issu_info_config_db_uuid,
        issu_contrail_config.issu_keyspace_config_db_uuid,
        issu_contrail_config.logger)

    old_cassandra_info = ICCassandraInfo(
        args.old_cassandra_address_list,
        args.odb_prefix,
        issu_contrail_config.issu_info_config_db_uuid,
        issu_contrail_config.issu_keyspace_config_db_uuid,
        issu_contrail_config.logger)
    # Create instance of amqp
    new_amqp_info = ICAmqpInfo(
        args.new_rabbit_address_list,
        args.new_rabbit_port,
        args.new_rabbit_user,
        args.new_rabbit_password,
        args.new_rabbit_vhost,
        args.new_rabbit_ha_mode,
        args.new_rabbit_q_name)

    old_amqp_info = ICAmqpInfo(args.old_rabbit_address_list,
                               args.old_rabbit_port,
                               args.old_rabbit_user,
                               args.old_rabbit_password,
                               args.old_rabbit_vhost,
                               args.old_rabbit_ha_mode,
                               args.old_rabbit_q_name)

    issu_rmq = ICRMQMain(
        old_cassandra_info, new_cassandra_info, old_amqp_info, new_amqp_info,
        ast.literal_eval(args.new_api_info), issu_contrail_config.logger)
    while (1):
        time.sleep(1)

if __name__ == "__main__":
    _issu_rmq_main()
