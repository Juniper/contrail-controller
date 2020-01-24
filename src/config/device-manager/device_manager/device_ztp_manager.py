#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import base64
from builtins import object
from builtins import str
import os
import psutil
import re
import socket

from cfgm_common.exceptions import NoIdError
import docker
import gevent
from netaddr import IPAddress, IPNetwork


class DeviceZtpManager(object):

    EXCHANGE = 'device_ztp_exchange'
    CONFIG_FILE_ROUTING_KEY = 'device_ztp.config.file'
    TFTP_FILE_ROUTING_KEY = 'device_ztp.tftp.file'
    ZTP_REQUEST_ROUTING_KEY = 'device_ztp.request'
    ZTP_RESPONSE_ROUTING_KEY = 'device_ztp.response.'
    MESSAGE_TTL = 5 * 60

    _instance = None

    def __init__(self, amqp_client, db_conn, args, logger):
        """Init routine for ZTP manager."""
        DeviceZtpManager._instance = self
        self._client = None
        self._active = False
        self._amqp_client = amqp_client
        self._db_conn = db_conn
        self._dnsmasq_conf_dir = args.dnsmasq_conf_dir
        self._tftp_dir = args.tftp_dir
        self._dhcp_leases_file = args.dhcp_leases_file
        self._dnsmasq_in_container = args.dnsmasq_in_container
        self._timeout = args.ztp_timeout
        self._host_ip = args.host_ip
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
        if not self._dnsmasq_conf_dir or not self._tftp_dir:
            return
        self._initialized = True
        self._amqp_client.add_exchange(self.EXCHANGE)

        consumer = 'device_manager_ztp.%s.config_queue' % \
            socket.getfqdn()
        self._amqp_client.add_consumer(
            consumer, self.EXCHANGE, routing_key=self.CONFIG_FILE_ROUTING_KEY,
            callback=self.handle_config_file_request)

        consumer = 'device_manager_ztp.%s.tftp_queue' % \
            socket.getfqdn()
        self._amqp_client.add_consumer(consumer, self.EXCHANGE,
                                       routing_key=self.TFTP_FILE_ROUTING_KEY,
                                       callback=self.handle_tftp_file_request)
    # end _initialize

    def db_read(self, obj_type, obj_id, obj_fields=None,
                ret_readonly=False):
        try:
            (ok, cassandra_result) = self._db_conn.object_read(
                obj_type, [obj_id], obj_fields, ret_readonly=ret_readonly)
        except NoIdError as e:
            # if NoIdError is for obj itself (as opposed to say for parent
            # or ref), let caller decide if this can be handled gracefully
            # by re-raising
            if e._unknown_id == obj_id:
                raise

            return (False, str(e))

        return (ok, cassandra_result[0])
    # end db_read

    def db_read_list(self, obj_type, filters=None):
        try:
            (ok, cassandra_result, ret_marker) = self._db_conn.object_list(
                obj_type, filters=filters)
        except Exception as e:
            return (False, str(e))
        return (ok, cassandra_result)
    # end db_read_list

    def set_active(self):
        if not self._initialized:
            return

        self._active = True
        self._lease_pattern = re.compile(
            r"[0-9]+ ([:A-Fa-f0-9]+) ([0-9.]+) ([a-zA-Z0-9\-_]+|\*) .*",
            re.MULTILINE | re.DOTALL)
        consumer = 'device_manager_ztp.ztp_queue'
        self._amqp_client.add_consumer(
            consumer, self.EXCHANGE, routing_key=self.ZTP_REQUEST_ROUTING_KEY,
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
            self._logger.debug("ztp_request: headers %s, config %s" %
                               (str(headers), str(config)))
            action = headers.get('action')
            if action is None:
                return

            timeout = self._timeout
            file_name = headers.get('file_name')
            file_path = os.path.join(self._dnsmasq_conf_dir, file_name)

            if action == 'create':
                self._logger.info(
                    "Waiting for file %s to be created" % file_path)
                while timeout > 0 and not os.path.isfile(file_path):
                    timeout -= 1
                    gevent.sleep(1)
                self._restart_dnsmasq()
                self._read_dhcp_leases(headers.get('fabric_name'), config)
            elif action == 'delete':
                self._logger.info(
                    "Waiting for file %s to be deleted" % file_path)
                while timeout > 0 and os.path.isfile(file_path):
                    timeout -= 1
                    gevent.sleep(1)
                self._restart_dnsmasq()
            elif action == 'read':
                self._logger.info("Reading leases file")
                self._read_dhcp_leases(headers.get('fabric_name'), config)
        except Exception as e:
            self._logger.error("Error while handling ztp request %s" % repr(e))
    # end _ztp_request

    def _read_dhcp_leases(self, fabric_name, config):
        results = {}
        results['failed'] = False

        device_to_ztp = config.get('device_to_ztp')
        if device_to_ztp:
            device_count = len(device_to_ztp)
        else:
            device_count = config.get('device_count', 0)
        self._logger.info("Waiting for %s devices" % device_count)

        timeout = self._timeout
        matched_devices = {}
        while timeout > 0:
            timeout -= 1
            lease_table = {}
            filters = {'physical_router_managed_state': "dhcp"}
            # read the PRs that are in 'dhcp' state
            ok, pr_list = self.db_read_list("physical_router", filters)
            if not ok:
                self._logger.error("Error reading leases info from "
                                   "database: %s " % ok)
            for pr_fqname, pr_uuid in pr_list:
                result = {}
                try:
                    (ok, result) = self.db_read(
                        "physical_router", pr_uuid,
                        ['physical_router_management_mac',
                         'physical_router_management_ip',
                         'physical_router_hostname'])
                    if not ok:
                        msg = "Error while reading the physical router " \
                              "with id %s : %s" % (pr_uuid, result)
                        self._logger.error(msg)
                except NoIdError as ex:
                    self._logger.error("Device not found %%s: %s" % (
                        pr_uuid, str(ex)))
                except Exception as e:
                    self._logger.error("Exception while reading device %s "
                                       "%s" % (pr_uuid, str(e)))

                mac = result.get('physical_router_management_mac')
                ip_addr = result.get('physical_router_management_ip')
                host_name = result.get('physical_router_hostname')
                lease_table[mac] = (ip_addr, host_name)

            for mac, (ip_addr, host_name) in list(lease_table.items()):
                if self._within_dhcp_subnet(ip_addr, config) and \
                        self._within_ztp_devices(host_name, device_to_ztp):
                    matched_devices[mac] = (ip_addr, host_name)
            if len(matched_devices) >= device_count:
                break
            gevent.sleep(1)

        results['device_list'] = [
            {'ip_addr': ip_addr, 'mac': mac, 'host_name': host_name}
            for mac, (ip_addr, host_name) in list(matched_devices.items())
        ]

        if not results['device_list']:
            results['failed'] = True
        results['msg'] = ("Found {} devices, expected {} devices. Here are " +
                          "the devices found: {}").format(
            len(results['device_list']), device_count, results['device_list'])

        self._logger.info(results['msg'])
        routing_key = self.ZTP_RESPONSE_ROUTING_KEY + fabric_name
        self._amqp_client.publish(results, self.EXCHANGE,
                                  routing_key=routing_key)
    # end _read_dhcp_leases

    def _handle_file_request(self, body, message, dir):
        try:
            self._logger.debug("handle_file_request: headers %s" %
                               str(message.headers))
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
                    contents = base64.b64decode(body)
                    contents = contents.replace('<host_ip>',
                                                self._host_ip)
                    f.write(bytearray(contents))
            elif action == 'delete':
                self._logger.info("Deleting file %s" % file_path)
                if os.path.isfile(file_path):
                    os.remove(file_path)
        except Exception as e:
            self._logger.error("ZTP manager: Error handling file request %s" %
                               repr(e))
    # end _handle_file_request

    def _restart_dnsmasq(self):
        if self._dnsmasq_in_container:
            self._restart_dnsmasq_container()
            return

        self._restart_dnsmasq_process()
    # end _restart_dnsmasq

    def _restart_dnsmasq_process(self):
        for process in psutil.process_iter():
            if process.name() == "dnsmasq":
                self._logger.info("Restarting dnsmasq process: name = %s, pid = %d", process.name(), process.pid)
                process.terminate()
    # end _restart_dnsmasq_process

    def _restart_dnsmasq_container(self):
        if self._client is None:
            self._client = docker.from_env()
        self._logger.debug("Fetching all containers")
        all_containers = self._client.containers.list(all=True)
        vendor_domain = os.getenv('VENDOR_DOMAIN', 'net.juniper.contrail')
        for container in all_containers:
            labels = container.labels or dict()
            service = labels.get(vendor_domain + '.service')
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
            ip_network = "{}/{}".format(ip_prefix, length)
            if IPAddress(ip_addr) in IPNetwork(ip_network):
                return True
        return False
    # end _within_dhcp_subnet

    @staticmethod
    def _within_ztp_devices(host_name, device_to_ztp):
        if not device_to_ztp or host_name == "*":
            return True
        return any([
            host_name == i.get('serial_number') for i in device_to_ztp
        ])
    # end _within_ztp_devices

# end DeviceZtpManager
