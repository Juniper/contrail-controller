#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import time
import os
import paramiko
import logging
import ast
from cfgm_common.vnc_kombu import VncKombuClient
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from contrail_issu.issu_contrail_common import ICCassandraClient
from contrail_issu.issu_contrail_common import ICCassandraInfo
from contrail_issu import issu_contrail_config


class ICAmqpInfo():
    def __init__(self, amqp_ip, amqp_port, amqp_user, amqp_pwd, amqp_vhost,
                 amqp_ha, amqp_q, amqp_use_ssl, amqp_ssl_version,
                 amqp_ssl_ca_certs, amqp_ssl_keyfile, amqp_ssl_certfile):
        self.amqp_ip = amqp_ip
        self.amqp_port = amqp_port
        self.amqp_user = amqp_user
        self.amqp_pwd = amqp_pwd
        self.amqp_vhost = amqp_vhost
        self.amqp_ha = amqp_ha
        self.amqp_q = amqp_q
        self.amqp_use_ssl = amqp_use_ssl
        self.amqp_ssl_version = amqp_ssl_version
        self.amqp_ssl_ca_certs = amqp_ssl_ca_certs
        self.amqp_ssl_keyfile = amqp_ssl_keyfile
        self.amqp_ssl_certfile = amqp_ssl_certfile

    def __str__(self):
        return (
            "ip = %s, port = %s, amqp_user = %s, amqp_pwd = %s, \
            amqp_vhost = %s, amqp_ha = %s, amqp_q = %s, amqp_use_ssl = %s, \
            amqp_ssl_version = %s, amqp_ssl_ca_certs = %s, \
            amqp_ssl_keyfile = %s, amqp_ssl_certfile = %s" %
            (str(self.amqp_ip), str(self.amqp_port), str(self.amqp_user),
             str(self.amqp_pwd), str(self.amqp_vhost), str(self.amqp_ha),
             str(self.amqp_q), str(self.amqp_use_ssl),
             str(self.amqp_ssl_version), str(self.amqp_ssl_ca_certs),
             str(self.amqp_ssl_keyfile), str(self.amqp_ssl_certfile)))


class ICKombuClient(VncKombuClient):
    def __init__(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, q_name, subscribe_cb, logger,
                 cassandra_inst, keyspace, new_api_info, **ssl_options):

        self._cassandra_inst = cassandra_inst
        self._keyspace = keyspace
        self._new_api_info = new_api_info
        self.logger = logger
        super(ICKombuClient, self).__init__(
            rabbit_ip, rabbit_port, rabbit_user, rabbit_password, rabbit_vhost,
            rabbit_ha_mode, q_name, subscribe_cb, logger, **ssl_options)

    def _reinit_control(self):
        vendor_domain = os.getenv('VENDOR_DOMAIN', 'net.juniper.contrail')
        cmd_cid = ('sudo docker ps'
                   ' --filter "label=' + vendor_domain + '.pod=control"'
                   ' --filter "label=' + vendor_domain + '.service=control"'
                   ' -q')
        cmd_reinit = 'sudo docker kill --signal="SIGUSR1" {}'

        for addr, clist in self._new_api_info.items():
            ssh = paramiko.SSHClient()
            ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            ssh.connect(addr, username=clist[0], password=clist[1])

            _, ssh_stdout, ssh_stderr = ssh.exec_command(cmd_cid)
            cid = ssh_stdout.readlines()[0]
            self.logger("Control on node {} has CID {}".format(addr, cid),
                        level=SandeshLevel.SYS_DEBUG)

            cmd = cmd_reinit.format(cid)
            _, ssh_stdout, ssh_stderr = ssh.exec_command(cmd)
            exit_status = ssh_stdout.channel.recv_exit_status()
            ssh_cmd_out = ssh_stdout.readlines()
            ssh_cmd_err = ssh_stderr.readlines()
            self.logger(
                'Signal sent to process. exit_code = {}, stdout = "{}",'
                ' stderr="{}"'.format(exit_status, ssh_cmd_out, ssh_cmd_err),
                level=SandeshLevel.SYS_INFO)
            ssh.close()

    def prepare_to_consume(self):
        try:
            self.logger("Config sync initiated...",
                        level=SandeshLevel.SYS_INFO)
            print("Config Sync initiated...")
            self._cassandra_inst.issu_merge_copy(self._keyspace)
            self.logger("Config sync done...", level=SandeshLevel.SYS_INFO)
            print("Config Sync done...")
        except Exception as e:
            self.logger(e, level=SandeshLevel.SYS_INFO)
        self.logger("Started runtime sync...", level=SandeshLevel.SYS_INFO)

        print("Started runtime sync...")
        self._reinit_control()

        self.logger("Start Compute upgrade...", level=SandeshLevel.SYS_INFO)
        print("Start Compute upgrade...")


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
        self.logger(msg, level=SandeshLevel.SYS_INFO)

    def issu_amqp_callback_handler(self, msg):
        self.logger(msg, level=SandeshLevel.SYS_INFO)
        if self.rmq_issu_handler is not None:
            msg = self.rmq_issu_handler(msg)
        msg['type'] = msg['type'].replace('-', '_')
        self.issu_cass_config_db_uuid_handle.issu_sync_row(msg,
                                                           'obj_uuid_table')
        # If fq_name is not present in the message, get it from DB
        if not msg.get('fq_name', None):
            obj_dict = self.issu_cass_config_db_uuid_handle.issu_read_row(msg)
            if obj_dict.get('fq_name', None):
                msg['fq_name'] = obj_dict['fq_name']

        self.amqp_new_version_handle.publish(msg)

    def start(self):
        # Create an instance of issu casandra_config_db_uuid
        self.issu_cass_config_db_uuid_handle = ICCassandraClient(
            self.old_cassandra.addr_info, self.new_cassandra.addr_info,
            self.old_cassandra.user, self.old_cassandra.password,
            self.new_cassandra.user, self.new_cassandra.password,
            self.old_cassandra.use_ssl, self.old_cassandra.ca_certs,
            self.new_cassandra.use_ssl, self.new_cassandra.ca_certs,
            self.old_cassandra.db_prefix, self.new_cassandra.db_prefix,
            self.cassandra_issu_info, self.logger)
        # Prepare it for Issu

        # Establish a amqp connection with newerversion
        self.logger(
            "Initiating amqp connection with new server...",
            level=SandeshLevel.SYS_DEBUG)
        self.logger(
            "Connection settings: %s" % (str(self.new_rabbit)),
            level=SandeshLevel.SYS_DEBUG)
        self.amqp_new_version_handle = VncKombuClient(
            self.new_rabbit.amqp_ip,
            self.new_rabbit.amqp_port,
            self.new_rabbit.amqp_user,
            self.new_rabbit.amqp_pwd,
            self.new_rabbit.amqp_vhost,
            self.new_rabbit.amqp_ha,
            self.new_rabbit.amqp_q,
            self.amqp_callback_handler,
            self.logger,
            rabbit_use_ssl=self.new_rabbit.amqp_use_ssl,
            kombu_ssl_version=self.new_rabbit.amqp_ssl_version,
            kombu_ssl_keyfile=self.new_rabbit.amqp_ssl_keyfile,
            kombu_ssl_certfile=self.new_rabbit.amqp_ssl_certfile,
            kombu_ssl_ca_certs=self.new_rabbit.amqp_ssl_ca_certs)
        self.logger("amqp connection initiated successfully with new server",
                    level=SandeshLevel.SYS_DEBUG)
        # Create a amqp connection with oldversion, passing all the information
        self.logger(
            "Initiating amqp connection with old server...",
            level=SandeshLevel.SYS_DEBUG)
        self.logger(
            "Connection settings: %s" % (str(self.old_rabbit)),
            level=SandeshLevel.SYS_DEBUG)
        self.amqp_old_version_handle = ICKombuClient(
            self.old_rabbit.amqp_ip,
            self.old_rabbit.amqp_port,
            self.old_rabbit.amqp_user,
            self.old_rabbit.amqp_pwd,
            self.old_rabbit.amqp_vhost,
            self.old_rabbit.amqp_ha,
            self.old_rabbit.amqp_q,
            self.issu_amqp_callback_handler,
            self.logger,
            self.issu_cass_config_db_uuid_handle,
            self.keyspace_info,
            self.new_api_info,
            rabbit_use_ssl=self.old_rabbit.amqp_use_ssl,
            kombu_ssl_version=self.old_rabbit.amqp_ssl_version,
            kombu_ssl_keyfile=self.old_rabbit.amqp_ssl_keyfile,
            kombu_ssl_certfile=self.old_rabbit.amqp_ssl_certfile,
            kombu_ssl_ca_certs=self.old_rabbit.amqp_ssl_ca_certs)
        self.logger("amqp connection initiated successfully with old server",
                    level=SandeshLevel.SYS_DEBUG)


def _issu_rmq_main():
    # Create Instance of cassandra info
    logging.basicConfig(
        level=logging.INFO,
        filename='/var/log/contrail/issu_contrail_run_sync.log',
        format='%(asctime)s %(message)s')
    args, remaining_args = issu_contrail_config.parse_args()
    new_cassandra_info = ICCassandraInfo(
        args.new_cassandra_address_list,
        args.new_cassandra_user,
        args.new_cassandra_password,
        args.new_cassandra_use_ssl,
        args.new_cassandra_ca_certs,
        args.ndb_prefix,
        issu_contrail_config.issu_info_config_db_uuid,
        issu_contrail_config.issu_keyspace_config_db_uuid,
        issu_contrail_config.logger)

    old_cassandra_info = ICCassandraInfo(
        args.old_cassandra_address_list,
        args.old_cassandra_user,
        args.old_cassandra_password,
        args.old_cassandra_use_ssl,
        args.old_cassandra_ca_certs,
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
        args.new_rabbit_q_name,
        args.new_rabbit_use_ssl,
        args.new_rabbit_ssl_version,
        args.new_rabbit_ssl_ca_certs,
        args.new_rabbit_ssl_keyfile,
        args.new_rabbit_ssl_certfile)

    old_amqp_info = ICAmqpInfo(args.old_rabbit_address_list,
                               args.old_rabbit_port,
                               args.old_rabbit_user,
                               args.old_rabbit_password,
                               args.old_rabbit_vhost,
                               args.old_rabbit_ha_mode,
                               args.old_rabbit_q_name,
                               args.old_rabbit_use_ssl,
                               args.old_rabbit_ssl_version,
                               args.old_rabbit_ssl_ca_certs,
                               args.old_rabbit_ssl_keyfile,
                               args.old_rabbit_ssl_certfile)

    _ = ICRMQMain(
        old_cassandra_info, new_cassandra_info, old_amqp_info, new_amqp_info,
        ast.literal_eval(args.new_api_info), issu_contrail_config.logger)
    while (1):
        time.sleep(1)


if __name__ == "__main__":
    _issu_rmq_main()
