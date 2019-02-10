#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import base64
import docker
import gevent
import os
import re
import socket
from netaddr import IPAddress, IPNetwork


class DeviceZtpManager(object):

    EXCHANGE = 'device_ztp_exchange'
    CONFIG_FILE_ROUTING_KEY = 'device_ztp.config.file'
    TFTP_FILE_ROUTING_KEY = 'device_ztp.tftp.file'
    ZTP_REQUEST_ROUTING_KEY = 'device_ztp.request'
    ZTP_RESPONSE_ROUTING_KEY = 'device_ztp.response.'
    MESSAGE_TTL = 5*60

    _instance = None

    def __init__(self, amqp_client, args, logger):
        DeviceZtpManager._instance = self
        self._client = None
        self._active = False
        self._amqp_client = amqp_client
        self._dnsmasq_conf_dir = args.dnsmasq_conf_dir
        self._tftp_dir = args.tftp_dir
        self._dhcp_leases_file = args.dhcp_leases_file
        self._timeout = args.ztp_timeout
        self._logger = logger
        self._lease_pattern = None
        self._initialized = False
        self._initialize()
    # end __init__

    @classmethod
    def get_instance(cls):
        return cls._instance
     # end get_instance

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if not inst:
            return
        cls._instance = None
    # end destroy_instance

    def _initialize(self):
        if not self._dnsmasq_conf_dir or not self._tftp_dir or\
                not self._dhcp_leases_file:
            return
        self._initialized = True
        self._amqp_client.add_exchange(self.EXCHANGE)

        consumer = 'device_manager_ztp.%s.config_queue' % \
            socket.getfqdn()
        self._amqp_client.add_consumer(consumer, self.EXCHANGE,
            routing_key=self.CONFIG_FILE_ROUTING_KEY,
            callback=self.handle_config_file_request)

        consumer = 'device_manager_ztp.%s.tftp_queue' % \
            socket.getfqdn()
        self._amqp_client.add_consumer(consumer, self.EXCHANGE,
            routing_key=self.TFTP_FILE_ROUTING_KEY,
            callback=self.handle_tftp_file_request)
    # end _initialize

    def set_active(self):
        if not self._initialized:
            return

        self._active = True
        self._lease_pattern = re.compile(
            r"[0-9]+ ([:A-Fa-f0-9]+) ([0-9.]+) .*",
            re.MULTILINE | re.DOTALL)
        consumer = 'device_manager_ztp.ztp_queue'
        self._amqp_client.add_consumer(consumer, self.EXCHANGE,
            routing_key=self.ZTP_REQUEST_ROUTING_KEY,
            callback=self.handle_ztp_request)
    # end set_active

    def handle_config_file_request(self, body, message):
        self._handle_file_request(body, message, self._dnsmasq_conf_dir)
    # end handle_config_file_request

    def handle_tftp_file_request(self, body, message):
        self._handle_file_request(body, message, self._tftp_dir)
    # end handle_tftp_file_request

    def handle_ztp_request(self, body, message):
        self._logger.debug("Entered handle_ztp_request")
        message.ack()
        gevent.spawn(self._ztp_request, message.headers, body)
    # end handle_ztp_request

    def _ztp_request(self, headers, config):
        try:
            self._logger.debug("ztp_request: headers %s, config %s" % \
                (str(headers), str(config)))
            action = headers.get('action')
            if action is None:
                return

            timeout = self._timeout
            file_name = headers.get('file_name')
            file_path = os.path.join(self._dnsmasq_conf_dir, file_name)

            if action == 'create':
                self._logger.info("Waiting for file %s to be created" % file_path)
                while timeout > 0 and not os.path.isfile(file_path):
                    timeout -= 1
                    gevent.sleep(1)
                self._restart_dnsmasq_container()
                self._read_dhcp_leases(headers.get('fabric_name'), config)
            elif action == 'delete':
                self._logger.info("Waiting for file %s to be deleted" % file_path)
                while timeout > 0 and os.path.isfile(file_path):
                    timeout -= 1
                    gevent.sleep(1)
                self._restart_dnsmasq_container()
        except Exception as e:
            self._logger.error("Error while handling ztp request %s" % repr(e))
    # end _ztp_request

    def _read_dhcp_leases(self, fabric_name, config):
        results = {}
        results['failed'] = True

        device_count = config.get('device_count', 0)
        timeout = self._timeout
        self._logger.info("Waiting for %s devices" % device_count)
        while timeout > 0:
            timeout -= 1
            results['device_list'] = []
            lease_table = {}

            if os.path.isfile(self._dhcp_leases_file):
                with open(self._dhcp_leases_file) as lfile:
                    for match in self._lease_pattern.finditer(lfile.read()):
                        mac = match.group(1)
                        ip_addr = match.group(2)
                        lease_table[mac] = ip_addr

            for mac, ip_addr in lease_table.iteritems():
                if self._within_dhcp_subnet(ip_addr, config):
                    results['device_list'].append({"ip_addr": ip_addr, "mac": mac})

            if len(results['device_list']) >= device_count:
                results['failed'] = False
                break

            gevent.sleep(1)

        results['msg'] = "Found {} devices, expected {} devices".\
            format(len(results['device_list']), device_count)
        self._logger.info(results['msg'])

        self._amqp_client.publish(results, self.EXCHANGE,
            routing_key=self.ZTP_RESPONSE_ROUTING_KEY + fabric_name)
    # end _read_dhcp_leases

    def _handle_file_request(self, body, message, dir):
        try:
            self._logger.debug("handle_file_request: headers %s" % str(message.headers))
            message.ack()
            action = message.headers.get('action')
            if action is None:
                return
            file_name = message.headers.get('file_name')
            if file_name is None or len(file_name) == 0:
                return

            action = str(action).lower()
            file_path = os.path.join(dir, file_name)
            if action == 'create':
                self._logger.info("Creating file %s" % file_path)
                with open(file_path, 'w') as f:
                    f.write(bytearray(base64.b64decode(body)))
            elif action == 'delete':
                self._logger.info("Deleting file %s" % file_path)
                os.remove(file_path)
        except Exception as e:
            self._logger.error("ZTP manager: Error handling file request %s" %
                               repr(e))
    # end _handle_file_request

    def _restart_dnsmasq_container(self):
        if self._client is None:
            self._client = docker.from_env()
        self._logger.debug("Fetching all containers")
        all_containers = self._client.containers.list(all=True)
        for container in all_containers:
            labels = container.labels or dict()
            service = labels.get('net.juniper.contrail.service')
            if service == 'dnsmasq':
                self._logger.info("Restarting dnsmasq docker: %s" %
                                  str(container.name))
                container.restart()
    # end _restart_dnsmasq_container

    @staticmethod
    def _within_dhcp_subnet(ip_addr, config):
        subnets = config.get('ipam_subnets', [])
        for subnet_obj in subnets:
            subnet = subnet_obj.get('subnet', {})
            ip_prefix = subnet.get('ip_prefix')
            length = subnet.get('ip_prefix_len')
            if IPAddress(ip_addr) in IPNetwork("{}/{}".format(ip_prefix, length)):
                return True
        return False
    # end _within_dhcp_subnet

# end DeviceZtpManager
