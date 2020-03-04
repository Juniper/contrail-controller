from __future__ import absolute_import
from __future__ import print_function

import argparse
from builtins import filter
from builtins import object
from builtins import str
import copy
from functools import wraps
import inspect
from io import StringIO
import itertools
import logging
import os
import re
import ssl
import sys

import cfgm_common
try:
    from cfgm_common import BGP_RTGT_ALLOC_PATH_TYPE0
    from cfgm_common import BGP_RTGT_ALLOC_PATH_TYPE1_2
except ImportError:
    # must be older release, assigning old path
    BGP_RTGT_ALLOC_PATH_TYPE0 = '/id/bgp/route-targets'
    BGP_RTGT_ALLOC_PATH_TYPE1_2 = '/id/bgp/route-targets'
try:
    from cfgm_common import get_bgp_rtgt_min_id
except ImportError:
    # must be older release, assigning default min ID
    def get_bgp_rtgt_min_id(asn):
        return 8000000
from cfgm_common import jsonutils as json
from cfgm_common.svc_info import _VN_SNAT_PREFIX_NAME
try:
    from cfgm_common import vnc_cgitb
except ImportError:
    import cgitb as vnc_cgitb
from cfgm_common.utils import cgitb_hook
from cfgm_common.zkclient import IndexAllocator
from cfgm_common.zkclient import ZookeeperClient
from future import standard_library
standard_library.install_aliases()  # noqa
import kazoo.client
import kazoo.exceptions
from netaddr import IPAddress, IPNetwork
from netaddr.core import AddrFormatError
from past.builtins import basestring
import pycassa
from pycassa.cassandra.ttypes import ConsistencyLevel
import pycassa.connection
from pycassa.connection import default_socket_factory
import schema_transformer.db
from thrift.transport import TSSLSocket
from collections import OrderedDict
from cfgm_common.db_json_exim import DatabaseJSONExim


if sys.version_info[0] < 3:
    reload(sys)  # noqa
    sys.setdefaultencoding('UTF8')

if __name__ == '__main__' and __package__ is None:
    parent = os.path.abspath(os.path.dirname(__file__))
    try:
        sys.path.remove(str(parent))
    except ValueError:  # Already removed
        pass
    import vnc_cfg_api_server  # noqa
    __package__ = 'vnc_cfg_api_server'  # noqa


from . import utils  # noqa
try:
    from .vnc_db import VncServerCassandraClient
except ImportError:
    from vnc_cfg_ifmap import VncServerCassandraClient


__version__ = "1.27"
"""
NOTE: As that script is not self contained in a python package and as it
supports multiple Contrail releases, it brings its own version that needs to be
manually updated each time it is modified. We also maintain a change log list
in that header:
* 1.27:
  - Fix import statement compatibility for python 2 and reorganise them
* 1.26:
  - Completed CEM-5586
  - Now, healing, checking, and cleaning works with JSON directly
  - No further need to have an active connection to cassandra or zookeeper is needed.
  - Healing and cleaning operations are directly performed on the Database dump provided by the client and the
    healed/cleaned json is produced.
* 1.25:
  - Fix route target validation code when VN RT list is set to none
* 1.24:
  - Fix pycassa import to support new UT framework
* 1.23:
  - Fix check RT backrefs to RI (CEM-9625)
* 1.22:
  - Typo fix for stale RT backrefs to RI
* 1.21:
  - Add new check and clean methods for RT backrefs to RI
* 1.20:
  - Fix SG ID allocation audit for SG __no_rule__ CEM-8607
* 1.19:
  - Fix typo at self.global_asn. CEM-8222
* 1.18
  - v1.15 is not compatilible to older releases. Restore compatability
* 1.17
  - Use cfgm_common.utils.decode_string to make obj_fq_name_str in same format
    as fq_name_str
* 1.16
  - The heal_fq_name_index does not properly extract resource UUID from the
    FQ name index table
* 1.15
  - Fix CEM-6463, handle cassandra_use_ssl properly
* 1.14
  - Fix get_subnet to fetch only necessary IPAM properties to prevent case
    where IPAM have a large number of ref/back-ref/children
* 1.13
  - Retrieve Subnet from IPAM if the ipam-method is flat-subnet
  - PEP8 compliance
* 1.12
  - Use TLSv1.2 for cassandra's connection
* 1.11
  - Make Individual connection timeout and buffer size user configurable
* 1.10
  - Add support SSL/TLS connection to cassandra DB
* 1.9
  - Add support to remove stale ref entries from obj_uuid_table of cassandra
* 1.8
  - Return error if a IIP/FIP/AIP have a stale VN referenced
* 1.7
  - Add support to detect and clean malformed route targets
  - Add support to detect and clean stale route targets listed in virtual
    network or logical router
* 1.6:
  - fix issue in 'clean_subnet_addr_alloc' method with IPv6 subnet
* 1.5:
  - fix bug to identifying stale route target when it's a RT of a LR with a
    gateway
* 1.4:
  - add timestamp into script output headers
  - remove verbose option and set default logging level to INFO
  - log output to local file (default: /var/log/contrail/db_manage.log) in
    addition to stdout
* 1.3:
  - Fix issue in the VN/subnet/IP address zookeeper lock clean method
    'clean_subnet_addr_alloc' which tried to clean 2 times same stale lock
* 1.2:
  - Re-define the way the script recovers the route target inconsistencies.
    The source of trust move from the schema transformer DB to the
    zookeeper DB. If Config or Schema DBs contains a RT not locked in
    zookeeper, it removes it. It does not heal missing RT in Config and
    Schema DBs, it lets the schema transformer takes care of that when it'll
    be re-initialized. It also cleans stale lock in zookeeper.
* 1.1:
  - Fix RT duplicate detection from schema cassandra DB
* 1.0:
  - add check to detect duplicate FQ name
  - add clean method to remove object not indexed in FQ name table if its
    FQ name is already used by another object or if multiple stale object
    use the same FQ name
  - heal FQ name index if FQ name not already used
  - when healing ZK ID lock path if the node already exists, update it
    with correct value
"""

try:
    VN_ID_MIN_ALLOC = cfgm_common.VNID_MIN_ALLOC
except AttributeError:
    VN_ID_MIN_ALLOC = 1
SG_ID_MIN_ALLOC = cfgm_common.SGID_MIN_ALLOC


def _parse_rt(rt):
    if isinstance(rt, basestring):
        prefix, asn, target = rt.split(':')
    else:
        prefix, asn, target = rt
    if prefix != 'target':
        raise ValueError()
    target = int(target)
    if not asn.isdigit():
        try:
            IPAddress(asn)
        except AddrFormatError:
            raise ValueError()
    else:
        asn = int(asn)
    return asn, target

# All possible errors from audit
class AuditError(Exception):
    def __init__(self, msg):
        self.msg = msg
    # end __init__
# class AuditError

exceptions = [
    'ZkStandaloneError',
    'ZkStandaloneError',
    'ZkFollowersError',
    'ZkNodeCountsError',
    'CassWrongRFError',
    'FQNIndexMissingError',
    'FQNStaleIndexError',
    'FQNMismatchError',
    'MandatoryFieldsMissingError',
    'IpSubnetMissingError',
    'InvalidIPAMRef',
    'VirtualNetworkMissingError',
    'VirtualNetworkIdMissingError',
    'IpAddressMissingError',
    'IpAddressDuplicateError',
    'UseragentSubnetExtraError',
    'UseragentSubnetMissingError',
    'SubnetCountMismatchError',
    'SubnetUuidMissingError',
    'SubnetIdToKeyMissingError',
    'ZkSGIdMissingError',
    'ZkSGIdExtraError',
    'SG0UnreservedError',
    'SGDuplicateIdError',
    'ZkVNIdExtraError',
    'ZkVNIdMissingError',
    'VNDuplicateIdError',
    'RTDuplicateIdError',
    'RTMalformedError',
    'CassRTRangeError',
    'ZkRTRangeError',
    'RTbackrefError',
    'ZkIpMissingError',
    'ZkIpExtraError',
    'ZkSubnetMissingError',
    'ZkSubnetExtraError',
    'ZkVNMissingError',
    'ZkVNExtraError',
    'SchemaRTgtIdExtraError',
    'ConfigRTgtIdExtraError',
    'ZkRTgtIdExtraError',
    'OrphanResourceError',
    'ZkSubnetPathInvalid',
    'FqNameDuplicateError',
]
for exception_class in exceptions:
    setattr(sys.modules[__name__],
            exception_class,
            type(exception_class, (AuditError,), {}))

def get_operations():
    checkers = {}
    healers = {}
    cleaners = {}
    module = sys.modules[__name__]
    global_functions = inspect.getmembers(module, inspect.isfunction)
    for name, func in global_functions:
        if func.__dict__.get('is_checker', False):
            checkers.update({name: func})
        elif func.__dict__.get('is_healer', False):
            healers.update({name: func})
        elif func.__dict__.get('is_cleaner', False):
            cleaners.update({name: func})
    global_classes = inspect.getmembers(module, inspect.isclass)
    for cname, a_class in global_classes:
        class_methods = inspect.getmembers(a_class, predicate=inspect.ismethod)
        for name, method in class_methods:
            if method.__dict__.get('is_checker', False):
                checkers.update({name: method})
            elif method.__dict__.get('is_healer', False):
                healers.update({name: method})
            elif method.__dict__.get('is_cleaner', False):
                cleaners.update({name: method})

    operations = {'checkers': checkers,
                  'healers': healers,
                  'cleaners': cleaners}
    return operations

def format_help():
    operations = get_operations()
    help_msg = ''
    for operater in list(operations.keys()):
        help_msg += format_line("Supported %s," % operater, 0, 2)
        for name, oper_func in list(operations[operater].items()):
            if name.startswith('db_'):
                name = name.lstrip('db_')
            help_msg += format_line("%s" % name, 1, 1)
            help_msg += format_line("- %s" % oper_func.__doc__, 2, 1)
        help_msg += '\n'

    return help_msg

def format_line(line, indent=0, newlines=0):
    indent = "    " * indent
    newlines = "\n" * newlines
    return "%s%s%s" % (indent, line, newlines)

def format_oper(oper, lstr):
    return oper.lstrip(lstr).replace('_', ' ')

def format_description():
    example_check_operations = ["check_route_targets_id"]
    example_heal_operations = ["heal_route_targets_id"]
    example_clean_operations = ["clean_stale_route_target_id",
                                "clean_stale_route_target"]

    examples = format_line("EXAMPLES:", 0, 2)
    examples += format_line("Checker example,", 1, 2)
    for example in example_check_operations:
        examples += format_line("python db_manage.py %s" % example, 2, 1)
        examples += format_line(
            "- Checks and displays the list of stale and missing %s." %
            format_oper(example, 'check_'), 3, 2)

    examples += format_line("Healer examples,\n\n", 1, 2)
    for example in example_heal_operations:
        examples += format_line("python db_manage.py %s" % example, 2, 1)
        examples += format_line(
            "- Displays the list of missing %s to be healed(dry-run)." %
            format_oper(example, 'heal_'), 3, 2)
        examples += format_line("python db_manage.py --execute %s" % example,
                                2, 1)
        examples += format_line(
            "- Creates the missing %s in DB's with(--execute)." % format_oper(
                example, 'heal_'), 3, 2)

    examples += format_line("Cleaner examples,\n\n", 1, 2)
    for example in example_clean_operations:
        examples += format_line("python db_manage.py %s" % example, 2, 1)
        examples += format_line(
            "- Displays the list of %s to be cleaned(dry-run)." % format_oper(
                example, 'clean_'), 3, 2)
        examples += format_line("python db_manage.py --execute %s" % example,
                                2, 1)
        examples += format_line(
            "- Deletes the %s in DB's with(--execute)." % format_oper(
                example, 'clean_'), 3, 2)

    help = format_line("Where,", 1, 1)
    help += format_line("%s" % (example_check_operations +
                                example_heal_operations +
                                example_clean_operations), 2, 1)
    help += format_line("- are some of the supported operations listed in"
                        + " positional arguments.", 3, 1)
    help += format_line("DB's - refer to zookeeper and cassandra.", 2, 2)

    note = format_line("NOTES:", 0, 1)
    note += format_line("check, heal and clean operations are used" +
                        " to do all checker, healer and cleaner operations" +
                        " respectively.", 2, 2)

    doc = format_line("DOCUMENTAION:", 0, 1)
    doc += format_line("Documentaion for each supported operation is" +
                       " displayed in positional arguments below.", 2, 2)

    wiki = format_line("WIKI:", 0, 1)
    wiki += format_line(
            "https://github.com/Juniper/contrail-controller/wiki/Database-"
            "management-tool-to-check-and-fix-inconsistencies.", 2, 1)
    description = [examples, help, note, doc, wiki]

    return ''.join(description)

def _parse_args(args_str):
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawTextHelpFormatter,
        description=format_description())

    parser.add_argument('-v', '--version', action='version',
                        version='%(prog)s ' + __version__)
    parser.add_argument('operation', help=format_help())
    help = ("Path to contrail-api conf file, "
            "default /etc/contrail/contrail-api.conf")
    parser.add_argument(
        "--api-conf", help=help, default="/etc/contrail/contrail-api.conf")
    parser.add_argument(
        "--execute", help="Apply database modifications",
        action='store_true', default=False)
    parser.add_argument(
        "--debug", help="Run in debug mode, default False",
        action='store_true', default=False)

    parser.add_argument(
        "--skip_backup", help="Don't Take a backup before heal and clean operations and store in json backups directory." + \
            "all backups are stored under /var/tmp/json_backups/ encoded with IP address and timestamp",
        action='store_true', default=False)
    parser.add_argument(
        "--connection-timeout", type=float,
        help="Individual Connection timeout, in seconds",
        default=0.5)
    parser.add_argument(
        "--buffer-size", type=int,
        help="Number of rows fetched at once",
        default=1024)

    if os.path.isdir("/var/log/contrail"):
        default_log = "/var/log/contrail/db_manage.log"
    else:
        import tempfile
        default_log = '%s/contrail-db-manage.log' % tempfile.gettempdir()

    # New arguments added to support JSON checking and healing
    parser.add_argument(
        "--in_json", type=str,
        help="Input Customer JSON file to be healed, cleaned, or checked for discrepancies. " + \
            "Need to export with pretty-print option in db_json_exim.py if want to use JSON functionality\n" + \
            "Example db_json_exim usage: python db_json_exim.py --export-to db-dump.json pretty-print"
    )

    parser.add_argument(
        "--out_json", type=str,
        help="Output Customer JSON file after healing and cleaning."
    )

    parser.add_argument(
        "--log_file", help="Log file to save output, default '%(default)s'",
        default=default_log)

    args_obj, remaining_argv = parser.parse_known_args(args_str.split())
    _args = args_obj

    _api_args = utils.parse_args('-c %s %s' % (
        _args.api_conf, ' '.join(remaining_argv)))[0]

    return (_args, _api_args)
# end _parse_args

class IndexJSONAllocator(IndexAllocator):
    def __init__(self,  db_manager, path, size=0, start_idx=0,
            reverse=False, alloc_list=None, max_alloc=0):
        self._size = size
        self._start_idx = start_idx
        if alloc_list is None:
            self._alloc_list = [{'start': start_idx, 'end': start_idx+size}]
        else:
            sorted_alloc_list = sorted(alloc_list, key=lambda k: k['start'])
            self._alloc_list = sorted_alloc_list

        size = self._get_range_size(self._alloc_list)

        if max_alloc == 0:
            self._max_alloc = size
        else:
            self._max_alloc = max_alloc

        self._db_manager = db_manager
        self._path = path
        self._in_use = bitarray('0')
        self._reverse = reverse
        for idx in self._db_manager.zk_get_children(path):
            idx_int = self._get_bit_from_zk_index(int(idx))
            if idx_int >= 0:
                self._set_in_use(self._in_use, idx_int)
        # end for idx
    # end __init__

    # Override Implementation
    def alloc(self, value=None, pools=None):
        if pools:
            idx = self._alloc_from_pools(pools)
        else:
            # Allocates a index from the allocation list
            if self._in_use.all():
                idx = self._in_use.length()
                if idx > self._max_alloc:
                    raise ResourceExhaustionError()
                self._in_use.append(1)
            else:
                idx = self._in_use.index(0)
                self._in_use[idx] = 1

        idx = self._get_zk_index_from_bit(idx)
        try:
            # Create a node at path and return its integer value
            id_str = "%(#)010d" % {'#': idx}
            self._db_manager.zk_create(self._path + id_str, value)
            return idx
        except ResourceExistsError:
            return self.alloc(value, pools)

class DatabaseManager(object):
    OBJ_MANDATORY_COLUMNS = ['type', 'fq_name', 'prop:id_perms']
    BASE_VN_ID_ZK_PATH = '/id/virtual-networks'
    BASE_SG_ID_ZK_PATH = '/id/security-groups/id'
    BASE_SUBNET_ZK_PATH = '/api-server/subnets'

    KV_SUBNET_KEY_TO_UUID_MATCH = re.compile('(.* .*/.*)')

    def __init__(self, args='', api_args=''):
        self._args = args
        self._api_args = api_args

        self._logger = utils.ColorLog(logging.getLogger(__name__))
        log_level = 'DEBUG' if self._args.debug else 'INFO'
        self._logger.setLevel(log_level)
        logformat = logging.Formatter("%(asctime)s %(levelname)s: %(message)s")
        stdout = logging.StreamHandler(sys.stdout)
        stdout.setFormatter(logformat)
        self._logger.addHandler(stdout)
        logfile = logging.handlers.RotatingFileHandler(
            self._args.log_file, maxBytes=10000000, backupCount=5)
        logfile.setFormatter(logformat)
        self._logger.addHandler(logfile)
        cluster_id = self._api_args.cluster_id
        self.using_json = False
        self.backup = True
        self.out_json = None
        self.input_json = None

        # extract the names of the input and output files
        if self._args.in_json:
            self.input_json = self._args.in_json
            self.using_json = True

        if self._args.out_json:
            self.out_json = self._args.out_json

        if self._args.skip_backup is True:
            self.backup = False
        # cassandra connection
        self._cassandra_servers = self._api_args.cassandra_server_list
        self._db_info = VncServerCassandraClient.get_db_info() + \
            schema_transformer.db.SchemaTransformerDB.get_db_info()
        self._cf_dict = {}
        self.creds = None

        if self.using_json is False:
            if (self._api_args.cassandra_user is not None and
                    self._api_args.cassandra_password is not None):
                self.creds = {
                    'username': self._api_args.cassandra_user,
                    'password': self._api_args.cassandra_password,
                }

            socket_factory = pycassa.connection.default_socket_factory
            if ('cassandra_use_ssl' in self._api_args and
                    self._api_args.cassandra_use_ssl):
                socket_factory = self._make_ssl_socket_factory(
                    self._api_args.cassandra_ca_certs, validate=False)
        else:
            with open(self.input_json) as json_file:
                self.data = json.load(json_file)

            # extract both subdatabases in JSON file
            self.cassandra = self.data['cassandra']
            self.zookeeper = self.data['zookeeper']

        ''' Build All the Cassandra Data Structures needed for db operations '''
        for ks_name, cf_name_list in self._db_info:
            if cluster_id:
                full_ks_name = '%s_%s' % (cluster_id, ks_name)
            else:
                full_ks_name = ks_name

            if self.using_json is True:
                # extract the current database
                curr_db_dict = self.cassandra[ks_name]
            else:
                pool = pycassa.ConnectionPool(
                    keyspace=full_ks_name,
                    server_list=self._cassandra_servers,
                    prefill=False, credentials=self.creds,
                    socket_factory=socket_factory,
                    timeout=self._args.connection_timeout)

            for cf_name in cf_name_list:
                if self.using_json is True:
                    database_subset = curr_db_dict[cf_name]
                    self._cf_dict[cf_name] = {}
                    for key, val_dict in database_subset.items():
                        # loop over the actual data elements and populate
                        self._cf_dict[cf_name][key] = OrderedDict()
                        for tb_key, tb_val in val_dict.items():
                            self._cf_dict[cf_name][key][tb_key] = tb_val[0]
                else:
                    self._cf_dict[cf_name] = pycassa.ColumnFamily(
                        pool, cf_name,
                        read_consistency_level=ConsistencyLevel.QUORUM,
                        buffer_size=self._args.buffer_size)

        # Get the system global autonomous system
        self.global_asn = self.get_autonomous_system()

        # zookeeper connection
        self.base_vn_id_zk_path = cluster_id + self.BASE_VN_ID_ZK_PATH
        if self.global_asn > 0xFFFF:
            self.BASE_RTGT_ID_ZK_PATH = BGP_RTGT_ALLOC_PATH_TYPE1_2
        else:
            self.BASE_RTGT_ID_ZK_PATH = BGP_RTGT_ALLOC_PATH_TYPE0
        self.base_rtgt_id_zk_path = cluster_id + self.BASE_RTGT_ID_ZK_PATH
        self.base_sg_id_zk_path = cluster_id + self.BASE_SG_ID_ZK_PATH
        self.base_subnet_zk_path = cluster_id + self.BASE_SUBNET_ZK_PATH

        # Initialize Zookeeper Data Structures as per need
        if self.using_json is True:
            self.root_dir = self.zookeeper['/']
        else:
            self._zk_client = kazoo.client.KazooClient(self._api_args.zk_server_ip)
            self._zk_client.start()
    # end __init__

    def _make_ssl_socket_factory(self, ca_certs, validate=True):
        # copy method from pycassa library because no other method
        # to override ssl version
        def ssl_socket_factory(host, port):
            TSSLSocket.TSSLSocket.SSL_VERSION = ssl.PROTOCOL_TLSv1_2
            return TSSLSocket.TSSLSocket(host, port,
                                         ca_certs=ca_certs, validate=validate)
        return ssl_socket_factory

    ''' Helper functions to implement Zookeeper wrapper functions '''
    def get_path_list(self, path_id_str):
        return [x for x in path_id_str.split('/') if x != '' and x != 'TestDBAudit']

    def get_path_dict(self, dir_hierarchy, spl_idx):
        curr_dict = self.root_dir
        if spl_idx is not 0:
            dir_hierarchy = dir_hierarchy[:spl_idx]

        for i, folder in enumerate(dir_hierarchy):
            parent_dict = curr_dict
            try:
                curr_dict = curr_dict[folder]
            except KeyError:
                break
        return parent_dict, curr_dict, folder

    ''' End Zookeeper wrapper helper functions '''

    ''' Wrapper functions implemented to support KazooClient/Zookeeper Operations '''
    # wrapper function for Zookeeper Delete
    def zk_delete(self, path, recursive=False):
        if self.using_json is True:
            directory_hierarchy = self.get_path_list(path)
            folder_to_delete = directory_hierarchy[-1]
            p_dict, curr_dict, _ = self.get_path_dict(directory_hierarchy, 0)

            if recursive is True:
                p_dict[folder_to_delete] = None
            else:
                if self.zk_get_children(path) not in [[],{}]:
                    raise kazoo.exceptions.NotEmptyError
                else:
                    p_dict[directory_hierarchy[-1]] = None
        else:
            self._zk_client.delete(path=path, recursive=recursive)

    def zk_exists(self, path):
        if self.using_json:
            directory_hierarchy = self.get_path_list(path)
            folder_to_check = directory_hierarchy[-1]
            p_dict, curr_dict, _ = self.get_path_dict(directory_hierarchy, 0)
            if folder_to_check in p_dict:
                return p_dict[folder_to_check]
            else:
                return None
        else:
             return self._zk_client.exists(path=path)
    # wrapper function for zookeeper set
    def zk_set(self, path, value):
        if self.using_json is True:
            directory_hierarchy = self.get_path_list(path)
            directory_to_update = directory_hierarchy[-1]
            p_dict, curr_dict, folder = self.get_path_dict(directory_hierarchy, 0)
            if folder != directory_to_update:
                raise kazoo.exceptions.NoNodeError
            p_dict[directory_to_update] = value
        else:
            self._zk_client.set(path=path, value=value)

    def zk_create(self, path, value, makepath=False):
        if self.using_json:
            directory_hierarchy = self.get_path_list(path)
            update_folder = directory_hierarchy[-1]
            p_dict, c_dict, _folder = self.get_path_dict(directory_hierarchy, 0)
            if makepath:
                if self.zk_get(path):
                    raise kazoo.exceptions.NodeExistsError
                else:
                     p_dict[update_folder] = value
                if _folder != update_folder:
                    curr_dict = self.root_dir
                    for folder in directory_hierarchy:
                        parent_dict = curr_dict
                        if folder not in curr_dict:
                            curr_dict[folder] = {}
                        curr_dict = curr_dict[folder]
                    parent_dict[update_folder] = value
            else:
                p_dict[update_folder] = value.encode()
                if _folder != update_folder:
                    raise kazoo.exceptions.NoNodeError
        else:
            self._zk_client.create(path=path, value=value, makepath=makepath)

    # wrapper function that mimics zk client get
    def zk_get(self, path_id_str):
        if self.using_json:
            directory_hierarchy = self.get_path_list(path_id_str)
            _id = directory_hierarchy[-1]
            _, curr_dict, _ = self.get_path_dict(directory_hierarchy, -1)
            try:
                return curr_dict[_id]
            except KeyError:
                raise kazoo.exceptions.NoNodeError
        else:
            return self._zk_client.get(path_id_str)

    # wrapper function that mimics zk client get_children function
    def zk_get_children(self, path_id_str):
        if self.using_json:
            directory_hierarchy = self.get_path_list(path_id_str)
            last_dir = directory_hierarchy[-1]
            _, curr_dict, folder = self.get_path_dict(directory_hierarchy, 0)
            if folder != last_dir:
                return []
            # edge case where there are no ids
            if type(curr_dict) is list:
                return []
            else:
                return curr_dict.keys()
        else:
            return self._zk_client.get_children(path_id_str)
    ''' End of Wrapper functions for Zookeeper '''

    ''' Wrapper functions for PyCassa/Cassandra operations '''
    # PyCassa Insert Wrapper function
    def cassandra_get(self, table, t_key, column_start=None, column_finish=None, columns=None):
        if self.using_json is True:
            table_to_check = table.get(t_key, {})
            if columns:
                return OrderedDict({
                            (k if k in table_to_check.keys() else k):(table_to_check[k] if k in
                            table_to_check.keys() else json.dumps(dict())) for k in columns
                       })
            elif column_start:
                return OrderedDict({
                            (k if k in table_to_check.keys() else k):(table_to_check[k] if k in
                            table_to_check.keys() else json.dumps(dict()))for k in
                            filter(lambda k: column_start <= k <= column_finish, table_to_check.keys())
                       })
            else:
                return OrderedDict({
                            (k if k in table_to_check.keys() else k):(table_to_check[k] if k in
                            table_to_check.keys() else json.dumps(dict())) for k in table_to_check.keys()
                       })

        else:
            if columns:
                return table.get(key=t_key, columns=columns)
            else:
                return table.get(key=t_key, column_start=column_start, column_finish=column_finish)


    # PyCassa Insert Wrapper function
    def cassandra_insert(self, table, key, columns):
        if self.using_json is True:
            table[key] = columns
        else:
            table.insert(key, columns)

    # PyCassa Get Range Wrapper function
    def cassandra_get_range(self, table, columns=None, column_count=None):
        if self.using_json is True:
            key_list = table.keys()
            if key_list:
                row_table = table.get(key_list[0], {})
            else:
                row_table = {}
            row_table_keys = row_table.keys()

            if columns:
                keys_to_consider = [k for k in row_table_keys if k in columns]
            elif column_count:
                keys_to_consider = row_table_keys[:column_count+1]
            else:
                keys_to_consider = row_table_keys

            return itertools.chain((tb_key, OrderedDict({k:table[tb_key][k] for k in keys_to_consider
                            if k in table[tb_key]})) for tb_key in key_list)

        else:
            if columns:
                return table.get_range(columns=columns)
            elif column_count:
                return table.get_range(column_count=column_count)
            else:
                return table.get_range()

    # PyCassa XGet Wrapper function
    def cassandra_xget(self, table, tb_key, column_start=None, column_finish=None):
        if self.using_json is True:
            if not column_start:
                return table.get(tb_key, {}).items()
            else:
                return { k:v for k,v in table[tb_key].items() if column_start <= k <= column_finish }.items()
        else:
            if not column_start:
                return table.xget(tb_key)
            else:
                return table.xget(tb_key, column_start=column_start, column_finish=column_finish)

    # PyCassa remove wrapper function
    def cassandra_remove(self, table, key, columns=None):
        if self.using_json is True:
            key_list = table.keys()
            row_table = table[key_list[0]]
            row_table_keys = row_table.keys()

            if columns:
                keys_to_consider = [k for k in row_table_keys if k in columns]
            else:
                keys_to_consider = row_table_keys
            
            for tb_key in key_list:
                if tb_key is key:
                    for k in keys_to_consider:
                        if k in table[tb_key]:
                            del table[tb_key][k]
        else:
            if columns:
                table.remove(key, columns=columns)
            else:
                table.remove(key)
    ''' End of PyCassa Wrapper functions '''

    def get_autonomous_system(self):
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        cols = self.cassandra_get(
            fq_name_table,
            'global_system_config',
            column_start='default-global-system-config:',
            column_finish='default-global-system-config;')
        gsc_uuid = cols.popitem()[0].split(':')[-1]
        cols = self.cassandra_get(obj_uuid_table, gsc_uuid, columns=['prop:autonomous_system'])
        return int(json.loads(cols['prop:autonomous_system']))

    def audit_subnet_uuid(self):
        ret_errors = []

        # check in useragent table whether net-id subnet -> subnet-uuid
        # and vice-versa exist for all subnets
        ua_kv_cf = self._cf_dict['useragent_keyval_table']
        ua_subnet_info = {}
        for key, cols in self.cassandra_get_range(ua_kv_cf):
            mch = self.KV_SUBNET_KEY_TO_UUID_MATCH.match(key)
            if mch:  # subnet key -> uuid
                subnet_key = mch.group(1)
                subnet_id = cols['value']
                try:
                    reverse_map = ua_kv_cf.get(subnet_id)
                except (pycassa.NotFoundException, KeyError):
                    errmsg = "Missing id(%s) to key(%s) mapping in useragent"\
                             % (subnet_id, subnet_key)
                    ret_errors.append(SubnetIdToKeyMissingError(errmsg))
            else:  # uuid -> subnet key
                subnet_id = key
                subnet_key = cols['value']
                ua_subnet_info[subnet_id] = subnet_key
                try:
                    reverse_map = ua_kv_cf.get(subnet_key)
                except (pycassa.NotFoundException, KeyError):
                    # Since release 3.2, only subnet_id/subnet_key are store in
                    # key/value store, the reverse was removed
                    continue

        # check all subnet prop in obj_uuid_table to see if subnet-uuid exists
        vnc_all_subnet_info = {}
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        vn_row = self.cassandra_xget(fq_name_table, 'virtual_network')
        vn_uuids = [x.split(':')[-1] for x, _ in vn_row]
        for vn_id in vn_uuids:
            subnets, ua_subnets = self.get_subnets(vn_id)
            for subnet in ua_subnets:
                try:
                    vnc_all_subnet_info[subnet['subnet_uuid']] = '%s %s/%d' % (
                        vn_id,
                        subnet['subnet']['ip_prefix'],
                        subnet['subnet']['ip_prefix_len'])
                except KeyError as e:
                    errmsg = ('Missing key (%s) in ipam-subnet (%s) for '
                              ' vn (%s)' % (e, subnet, vn_id))
                    ret_errors.append(SubnetUuidMissingError(errmsg))
        return ua_subnet_info, vnc_all_subnet_info, ret_errors
    # end audit_subnet_uuid

    def audit_route_targets_id(self):
        logger = self._logger
        fq_name_table = self._cf_dict['obj_fq_name_table']
        uuid_table = self._cf_dict['obj_uuid_table']
        rt_table = self._cf_dict['route_target_table']
        ret_errors = []
        zk_set = set()
        schema_set = set()
        config_set = set()
        malformed_set = set()
        stale_list = {}

        # read in route-target ids from zookeeper
        base_path = self.base_rtgt_id_zk_path
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        num_bad_rts = 0
        for id in self.zk_get_children(base_path) or []:
            rt_zk_path = os.path.join(base_path, id)
            res_fq_name_str = self.zk_get(rt_zk_path)[0]
            id = int(id)
            zk_set.add((id, res_fq_name_str))
            if id < get_bgp_rtgt_min_id(self.global_asn):
                # ZK contain RT ID lock only for system RT
                errmsg = 'Wrong Route Target range in zookeeper %d' % id
                ret_errors.append(ZkRTRangeError(errmsg))
                num_bad_rts += 1
        logger.debug("Got %d Route Targets with ID in zookeeper %d from wrong "
                     "range", len(zk_set), num_bad_rts)

        # read route-targets from schema transformer cassandra keyspace
        logger.debug("Reading Route Target IDs from cassandra schema "
                     "transformer keyspace")
        num_bad_rts = 0
        for res_fq_name_str, cols in self.cassandra_get_range(rt_table, columns=['rtgt_num']):
            id = int(cols['rtgt_num'])
            if id < get_bgp_rtgt_min_id(self.global_asn):
                # Should never append
                msg = ("Route Target ID %d allocated for %s by the schema "
                       "transformer is not contained in the system range" %
                       (id, res_fq_name_str))
                ret_errors.append(ZkRTRangeError(msg))
                num_bad_rts += 1
            schema_set.add((id, res_fq_name_str))
        logger.debug("Got %d Route Targets with ID in schema DB %d from wrong "
                     "range", len(schema_set), num_bad_rts)

        # read in route-targets from API server cassandra keyspace
        logger.debug("Reading Route Target objects from cassandra API server "
                     "keyspace")
        user_rts = 0
        no_assoc_msg = "No Routing Instance or Logical Router associated"
        for fq_name_uuid_str, _ in self.cassandra_xget(fq_name_table, 'route_target'):
            fq_name_str, _, uuid = fq_name_uuid_str.rpartition(':')
            try:
                asn, id = _parse_rt(fq_name_str)
            except ValueError:
                malformed_set.add((fq_name_str, uuid))
            if (asn != self.global_asn or
                    id < get_bgp_rtgt_min_id(self.global_asn)):
                user_rts += 1
                continue  # Ignore user defined RT
            try:
                cols = self.cassandra_xget(uuid_table, uuid, column_start='backref:', column_finish='backref;')
            except (pycassa.NotFoundException, KeyError):
                continue
            backref_uuid = None
            for col, _ in cols:
                if col.startswith('backref:logical_router:'):
                    backref_uuid = col.rpartition(':')[-1]
                    break
                elif col.startswith('backref:routing_instance:'):
                    backref_uuid = col.rpartition(':')[-1]

            if not backref_uuid:
                config_set.add((id, no_assoc_msg))
                continue
            try:
                cols = self.cassandra_get(uuid_table, backref_uuid, columns=['fq_name'])
            except (pycassa.NotFoundException, KeyError):
                config_set.add((id, no_assoc_msg))
                continue
            config_set.add((id, ':'.join(json.loads(cols['fq_name']))))
        logger.debug("Got %d system defined Route Targets in cassandra and "
                     "%d defined by users", len(config_set), user_rts)

        # Check in VN and LR if user allocated RT are valid and not in the
        # system range
        num_user_rts = 0
        num_bad_rts = 0
        list_names = [
            'prop:route_target_list',
            'prop:import_route_target_list',
            'prop:export_route_target_list',
            'prop:configured_route_target_list',
        ]

        for fq_name_str_uuid, _ in itertools.chain(
                self.cassandra_xget(table=fq_name_table, tb_key='virtual_network'),
                self.cassandra_xget(table=fq_name_table, tb_key='logical_router')):
            fq_name_str, _, uuid = fq_name_str_uuid.rpartition(':')
            for list_name in list_names:
                try:
                    cols = self.cassandra_get(uuid_table, uuid, columns=[list_name])
                except (pycassa.NotFoundException, KeyError):
                    continue
                
                rts_col = json.loads(str(cols[list_name])) or {}                
                for rt in rts_col.get('route_target', []):
                    try:
                        asn, id = _parse_rt(rt)
                    except ValueError:
                        msg = ("Virtual Network or Logical Router %s(%s) %s "
                               "contains a malformed Route Target '%s'" %
                               (fq_name_str, uuid,
                                list_name[5:].replace('_', ' '), rt))
                        ret_errors.append(RTMalformedError(msg))
                        stale_list.setdefault(
                            (fq_name_str, uuid, list_name), set()).add(rt)

                    if (asn != self.global_asn or
                            id < get_bgp_rtgt_min_id(self.global_asn)):
                        num_user_rts += 1
                        continue  # all good
                    num_bad_rts += 1
                    msg = ("Virtual Network or Logical Router %s(%s) %s "
                           "contains a Route Target in a wrong range '%s'" %
                           (fq_name_str, uuid, list_name[5:].replace('_', ' '),
                            rt))
                    ret_errors.append(CassRTRangeError(msg))
                    stale_list.setdefault(
                        (fq_name_str, uuid, list_name), set()).add(rt)
        logger.debug("Got %d user configured route-targets, %d in bad range",
                     num_user_rts, num_bad_rts)

        return (zk_set, schema_set, config_set, malformed_set, stale_list,
                ret_errors)

    def get_stale_zk_rt(self, zk_set, schema_set, config_set):
        fq_name_table = self._cf_dict['obj_fq_name_table']
        stale_zk_entry = set()
        zk_set_copy = zk_set.copy()
        # in ZK but not in config and schema, stale entry => delete it in ZK
        stale_zk_entry |= (zk_set_copy - (schema_set & config_set))
        zk_set_copy -= (zk_set_copy - (schema_set & config_set))
        # in ZK and schema but not in config
        for id, res_fq_name_str in (zk_set_copy & schema_set) - config_set:
            try:
                self.cassandra_get(
                    fq_name_table,
                    'routing_instance',
                    column_start='%s:' % res_fq_name_str,
                    column_finish='%s;' % res_fq_name_str,
                )
            except (pycassa.NotFoundException, KeyError):
                stale_zk_entry.add((id, res_fq_name_str))
        # in ZK and config but not in schema, schema will fix it, nothing to do

        return stale_zk_entry

    def audit_security_groups_id(self):
        logger = self._logger
        ret_errors = []

        # read in security-group ids from zookeeper
        base_path = self.base_sg_id_zk_path
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        zk_all_sgs = {}
        for sg_id in self.zk_get_children(base_path) or []:
            sg_val = self.zk_get(base_path + '/' + sg_id)[0]

            # sg-id of 0 is reserved
            if int(sg_id) == 0:
                if sg_val != '__reserved__':
                    ret_errors.append(SG0UnreservedError(''))
                continue

            # Need due to the issue CEM-8607
            if sg_val == "[u'default-domain', u'default-project', '__no_rule__']":
                sg_val = 'default-domain:default-project:__no_rule__'

            zk_all_sgs[int(sg_id)] = sg_val

        logger.debug("Got %d security-groups with id", len(zk_all_sgs))

        # read in security-groups from cassandra to get id+fq_name
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading security-group objects from cassandra")
        sg_uuids = [x.split(':')[-1] for x, _ in
                    self.cassandra_xget(fq_name_table, 'security_group')]
        cassandra_all_sgs = {}
        duplicate_sg_ids = {}
        first_found_sg = {}
        missing_ids = set([])
        for sg_uuid in sg_uuids:
            try:
                sg_cols = self.cassandra_get(
                    obj_uuid_table,
                    sg_uuid, columns=['prop:security_group_id', 'fq_name'])
            except (pycassa.NotFoundException, KeyError):
                continue
            sg_fq_name_str = ':'.join(json.loads(sg_cols['fq_name']))
            if not sg_cols.get('prop:security_group_id'):
                errmsg = 'Missing security group id in cassandra for sg %s' \
                         % (sg_uuid)
                ret_errors.append(VirtualNetworkIdMissingError(errmsg))
                missing_ids.add((sg_uuid, sg_fq_name_str))
                continue
            sg_id = int(json.loads(sg_cols['prop:security_group_id']))
            if sg_id in first_found_sg:
                if sg_id < SG_ID_MIN_ALLOC:
                    continue
                duplicate_sg_ids.setdefault(
                    sg_id - SG_ID_MIN_ALLOC, [first_found_sg[sg_id]]).append(
                        (sg_fq_name_str, sg_uuid))
            else:
                cassandra_all_sgs[sg_id] = sg_fq_name_str
                first_found_sg[sg_id] = (sg_fq_name_str, sg_uuid)

        logger.debug("Got %d security-groups with id", len(cassandra_all_sgs))
        zk_set = set([(id, fqns) for id, fqns in list(zk_all_sgs.items())])
        cassandra_set = set([(id - SG_ID_MIN_ALLOC, fqns)
                             for id, fqns in list(cassandra_all_sgs.items())
                             if id >= SG_ID_MIN_ALLOC])

        return zk_set, cassandra_set, ret_errors, duplicate_sg_ids, missing_ids
    # end audit_security_groups_id

    def audit_virtual_networks_id(self):
        logger = self._logger
        ret_errors = []

        # read in virtual-network ids from zookeeper
        base_path = self.base_vn_id_zk_path
        logger.debug("Doing recursive zookeeper read from %s", base_path)
        zk_all_vns = {}
        for vn_id in self.zk_get_children(base_path) or []:
            vn_fq_name_str = self.zk_get(base_path + '/' + vn_id)[0]
            # VN-id in zk starts from 0, in cassandra starts from 1
            zk_all_vns[int(vn_id) + VN_ID_MIN_ALLOC] = vn_fq_name_str
        logger.debug("Got %d virtual-networks with id in ZK.", len(zk_all_vns))

        # read in virtual-networks from cassandra to get id+fq_name
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading virtual-network objects from cassandra")
        vn_uuids = [x.split(':')[-1] for x, _ in
                    self.cassandra_xget(fq_name_table, 'virtual_network')]
        cassandra_all_vns = {}
        duplicate_vn_ids = {}
        first_found_vn = {}
        missing_ids = set([])
        for vn_uuid in vn_uuids:
            try:
                vn_cols = self.cassandra_get(
                    obj_uuid_table,
                    vn_uuid,
                    columns=['prop:virtual_network_properties', 'fq_name',
                             'prop:virtual_network_network_id'],
                )
            except (pycassa.NotFoundException, KeyError):
                continue
            vn_fq_name_str = ':'.join(json.loads(vn_cols['fq_name']))
            try:
                vn_id = json.loads(vn_cols['prop:virtual_network_network_id'])
            except KeyError:
                try:
                    # upgrade case older VNs had it in composite prop
                    vn_props = json.loads(
                        vn_cols['prop:virtual_network_properties'])
                    vn_id = vn_props['network_id']
                except KeyError:
                    missing_ids.add((vn_uuid, vn_fq_name_str))
                    continue
            if not vn_id:
                missing_ids.add((vn_uuid, vn_fq_name_str))
                continue
            if vn_id in first_found_vn:
                duplicate_vn_ids.setdefault(
                    vn_id, [first_found_vn[vn_id]]).append(
                        (vn_fq_name_str, vn_uuid))
            else:
                cassandra_all_vns[vn_id] = vn_fq_name_str
                first_found_vn[vn_id] = (vn_fq_name_str, vn_uuid)

        logger.debug("Got %d virtual-networks with id in Cassandra.",
                     len(cassandra_all_vns))

        zk_set = set([(id, fqns) for id, fqns in list(zk_all_vns.items())])
        cassandra_set = set([(id, fqns)
                             for id, fqns in list(cassandra_all_vns.items())])

        return zk_set, cassandra_set, ret_errors, duplicate_vn_ids, missing_ids
    # end audit_virtual_networks_id

    def get_subnets(self, vn_id):
        """
        For a given vn_id, retrieve subnets based on IPAM type
        flat-subnet: Retrieve it from the network-ipam's ipam-subnet
        User-Agent KeyVal Table: for a flat subnet, User Agent keyval table
            is setup with 0.0.0.0/0 prefix and prefix-len value.
        Returns:
            subnet_dicts: Subnets derived from VN and IPAM subnets
            ua_subnet_dicts: Subnets derived from VN plus
                             Zero prefix (0.0.0.0/0) for IPAM subnets if
                             ipam method is flat-subnet
        """
        subnet_dicts = []
        ua_subnet_dicts = []
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        # find all subnets on this VN and add for later check
        ipam_refs = self.cassandra_xget(
                            obj_uuid_table, vn_id,
                            column_start='ref:network_ipam:',
                            column_finish='ref:network_ipam;'
                        )

        for network_ipam_ref, attr_json_dict in ipam_refs:
            try:
                network_ipam_uuid = network_ipam_ref.split(':')[2]
            except (IndexError, AttributeError) as e:
                msg = ("Exception (%s)\n"
                       "Unable to find Network IPAM UUID "
                       "for (%s). Invalid IPAM?" % (e, network_ipam_ref))
                raise InvalidIPAMRef(msg)

            try:
                network_ipam = self.cassandra_get(obj_uuid_table,
                    network_ipam_uuid, columns=['fq_name',
                                                'prop:ipam_subnet_method'])
            except (pycassa.NotFoundException, KeyError) as e:
                msg = ("Exception (%s)\n"
                       "Invalid or non-existing "
                       "UUID (%s)" % (e, network_ipam_uuid))
                raise FQNStaleIndexError(msg)

            ipam_method = network_ipam.get('prop:ipam_subnet_method')
            if isinstance(ipam_method, str):
                ipam_method = json.loads(ipam_method)

            attr_dict = json.loads(attr_json_dict)['attr']
            zero_prefix = {u'ip_prefix': u'0.0.0.0',
                           u'ip_prefix_len': 0}
            for subnet in attr_dict['ipam_subnets']:
                subnet.update([('ipam_method', ipam_method),
                               ('nw_ipam_fq',
                                network_ipam['fq_name'])])
                if 'subnet' in subnet:
                    subnet_dicts.append(subnet)
                if (ipam_method != 'flat-subnet' and
                        'subnet' in subnet):
                    ua_subnet_dicts.append(subnet)
                elif (ipam_method == 'flat-subnet' and
                      'subnet_uuid' in subnet):
                    # match fix for LP1646997
                    subnet.update([('subnet', zero_prefix)])
                    ua_subnet_dicts.append(subnet)
            if ipam_method == 'flat-subnet':
                ipam_subnets = self.cassandra_xget(
                    obj_uuid_table,
                    network_ipam_uuid,
                    column_start='propl:ipam_subnets:',
                    column_finish='propl:ipam_subnets;')

                for _, subnet_unicode in ipam_subnets:
                    sdict = json.loads(subnet_unicode)
                    sdict.update([('ipam_method', ipam_method),
                                  ('nw_ipam_fq',
                                   network_ipam['fq_name'])])
                    subnet_dicts.append(sdict)
        return subnet_dicts, ua_subnet_dicts

    def _addr_alloc_process_ip_objects(self, cassandra_all_vns, duplicate_ips,
                                       ip_type, ip_uuids):
        logger = self._logger
        ret_errors = []
        renamed_keys = []

        if ip_type == 'instance-ip':
            addr_prop = 'prop:instance_ip_address'
            vn_is_ref = True
        elif ip_type == 'floating-ip':
            addr_prop = 'prop:floating_ip_address'
            vn_is_ref = False
        elif ip_type == 'alias-ip':
            addr_prop = 'prop:alias_ip_address'
            vn_is_ref = False
        else:
            raise Exception('Unknown ip type %s' % (ip_type))

        # walk vn fqn index, pick default-gw/dns-server-addr
        # and set as present in cassandra
        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        def set_reserved_addrs_in_cassandra(vn_id, fq_name_str):
            if fq_name_str in cassandra_all_vns:
                # already parsed and handled
                return

            # find all subnets on this VN and add for later check
            subnets, _ = self.get_subnets(vn_id)
            cassandra_all_vns[fq_name_str] = {}
            for subnet in subnets:
                try:
                    sn_key = '%s/%s' % (subnet['subnet']['ip_prefix'],
                                        subnet['subnet']['ip_prefix_len'])
                    gw = subnet.get('default_gateway')
                    dns = subnet.get('dns_server_address')
                    cassandra_all_vns[fq_name_str][sn_key] = {
                        'ipam_method': subnet['ipam_method'],
                        'nw_ipam_fq': subnet['nw_ipam_fq'],
                        'start': subnet['subnet']['ip_prefix'],
                        'gw': gw,
                        'dns': dns,
                        'addrs': []}
                except KeyError as e:
                    errmsg = ('Missing key (%s) in ipam-subnet (%s) for '
                              'vn (%s)' % (e, subnet, vn_id))
                    raise SubnetUuidMissingError(errmsg)
        # end set_reserved_addrs_in_cassandra

        for fq_name_str_uuid, _ in self.cassandra_xget(obj_fq_name_table, 'virtual_network'):
            fq_name_str = ':'.join(fq_name_str_uuid.split(':')[:-1])
            vn_id = fq_name_str_uuid.split(':')[-1]
            set_reserved_addrs_in_cassandra(vn_id, fq_name_str)
        # end for all VNs

        for ip_id in ip_uuids:
            # get addr
            ip_cols = dict(self.cassandra_xget(obj_uuid_table, ip_id))
            if not ip_cols:
                errmsg = ('Missing object in uuid table for %s %s' %
                          (ip_type, ip_id))
                ret_errors.append(FQNStaleIndexError(errmsg))
                continue

            try:
                ip_addr = json.loads(ip_cols[addr_prop])
            except KeyError:
                errmsg = 'Missing ip addr in %s %s' % (ip_type, ip_id)
                ret_errors.append(IpAddressMissingError(errmsg))
                continue

            # get vn uuid
            vn_id = None
            if vn_is_ref:
                for col_name in list(ip_cols.keys()):
                    mch = re.match('ref:virtual_network:(.*)', col_name)
                    if not mch:
                        continue
                    vn_id = mch.group(1)
            else:
                vn_fq_name_str = ':'.join(json.loads(ip_cols['fq_name'])[:-2])
                vn_cols = self.cassandra_get(
                    obj_fq_name_table,
                    'virtual_network',
                    column_start='%s:' % (vn_fq_name_str),
                    column_finish='%s;' % (vn_fq_name_str))
                vn_id = list(vn_cols.keys())[0].split(':')[-1]

            if not vn_id:
                ret_errors.append(VirtualNetworkMissingError(
                    'Missing VN in %s %s.' % (ip_type, ip_id)))
                continue

            try:
                col = self.cassandra_get(obj_uuid_table, vn_id, columns=['fq_name'])
            except (pycassa.NotFoundException, KeyError):
                ret_errors.append(VirtualNetworkMissingError(
                    'Missing VN in %s %s.' % (ip_type, ip_id)))
                continue
            fq_name_str = ':'.join(json.loads(col['fq_name']))
            if fq_name_str not in cassandra_all_vns:
                msg = ("Found IP %s %s on VN %s (%s) thats not in FQ NAME "
                       "index" % (ip_type, ip_id, vn_id, fq_name_str))
                ret_errors.append(FQNIndexMissingError(msg))
                # find all subnets on this VN and add for later check
                set_reserved_addrs_in_cassandra(vn_id, fq_name_str)
            # end first encountering vn

            for sn_key in cassandra_all_vns[fq_name_str]:
                if not IPAddress(ip_addr) in IPNetwork(sn_key):
                    continue
                # gateway not locked on zk, we don't need it
                gw = cassandra_all_vns[fq_name_str][sn_key]['gw']
                if gw and IPAddress(ip_addr) == IPAddress(gw):
                    break
                addrs = cassandra_all_vns[fq_name_str][sn_key]['addrs']
                founded_ip_addr = [ip[0] for ip in addrs if ip[1] == ip_addr]
                if founded_ip_addr:
                    duplicate_ips.setdefault(fq_name_str, {}).\
                        setdefault(sn_key, {}).\
                        setdefault(ip_addr, founded_ip_addr).append(ip_id)
                    break
                else:
                    addrs.append((ip_id, ip_addr))
                    if ('ipam_method' in
                            cassandra_all_vns[fq_name_str][sn_key] and
                        cassandra_all_vns[fq_name_str][sn_key]['ipam_method']
                            == 'flat-subnet' and
                        'nw_ipam_fq' in
                            cassandra_all_vns[fq_name_str][sn_key]):
                        renamed_fq_name = ':'.join(json.loads(
                          cassandra_all_vns[fq_name_str][sn_key]['nw_ipam_fq']
                          ))
                        renamed_keys.append((fq_name_str, renamed_fq_name))
                    break
            else:
                errmsg = 'Missing subnet for ip %s %s' % (ip_type, ip_id)
                ret_errors.append(IpSubnetMissingError(errmsg))
            # end handled the ip
        # end for all ip_uuids

        # replace VN ID with subnet ID if ipam-method is flat-subnet
        # and has IIP
        for (vn_id, sn_id) in renamed_keys:
            if vn_id in cassandra_all_vns:
                cassandra_all_vns[sn_id] = copy.deepcopy(
                    cassandra_all_vns[vn_id])
                del cassandra_all_vns[vn_id]
        return ret_errors

    def _subnet_path_discovery(self, ret_errors, stale_zk_path):
        subnet_paths = set([])

        def deep_path_discovery(path):
            try:
                IPNetwork(path.split(':', 3)[-1])
            except AddrFormatError:
                try:
                    suffixes = self.zk_get_children(path)
                except (kazoo.exceptions.NoNodeError, KeyError):
                    self._logger.debug("ZK subnet path '%s' does not exits" %
                                       path)
                    return
                if not suffixes:
                    stale_zk_path.append(path)
                    return
                for suffix in suffixes:
                    deep_path_discovery('%s/%s' % (path, suffix))
            else:
                subnet_paths.add(path)

        deep_path_discovery(self.base_subnet_zk_path)
        return subnet_paths

    def audit_subnet_addr_alloc(self):
        ret_errors = []
        stale_zk_path = []
        logger = self._logger

        zk_all_vns = {}
        logger.debug("Doing recursive zookeeper read from %s",
                     self.base_subnet_zk_path)
        num_addrs = 0
        for subnet_path in self._subnet_path_discovery(
                ret_errors, stale_zk_path):
            if subnet_path.startswith("%s/" % self.base_subnet_zk_path):
                vn_subnet_name = subnet_path[
                    len("%s/" % self.base_subnet_zk_path):]
            else:
                vn_subnet_name = subnet_path
            vn_fq_name_str = ':'.join(vn_subnet_name.split(':', 3)[:-1])
            pfx = vn_subnet_name.split(':', 3)[-1]
            zk_all_vns.setdefault(vn_fq_name_str, {})
            pfxlens = self.zk_get_children(subnet_path)
            if not pfxlens:
                zk_all_vns[vn_fq_name_str][pfx] = []
                continue
            for pfxlen in pfxlens:
                subnet_key = '%s/%s' % (pfx, pfxlen)
                zk_all_vns[vn_fq_name_str][subnet_key] = []
                addrs = self.zk_get_children(
                    '%s/%s' % (subnet_path, pfxlen))
                if not addrs:
                    continue
                for addr in addrs:
                    iip_uuid = self.zk_get(
                        subnet_path + '/' + pfxlen + '/' + addr)
                    if iip_uuid is not None:
                        zk_all_vns[vn_fq_name_str][subnet_key].append(
                            (iip_uuid[0], str(IPAddress(int(addr)))))
                        num_addrs += 1
        # end for all subnet paths
        logger.debug("Got %d networks %d addresses",
                     len(zk_all_vns), num_addrs)

        logger.debug("Reading instance/floating-ip objects from cassandra")
        cassandra_all_vns = {}
        duplicate_ips = {}
        num_addrs = 0
        fq_name_table = self._cf_dict['obj_fq_name_table']

        iip_rows = self.cassandra_xget(fq_name_table, 'instance_ip')
        iip_uuids = [x.split(':')[-1] for x, _ in iip_rows]
        ret_errors.extend(self._addr_alloc_process_ip_objects(
            cassandra_all_vns, duplicate_ips, 'instance-ip', iip_uuids))

        num_addrs += len(iip_uuids)

        fip_rows = self.cassandra_xget(fq_name_table, 'floating_ip')
        fip_uuids = [x.split(':')[-1] for x, _ in fip_rows]
        ret_errors.extend(self._addr_alloc_process_ip_objects(
            cassandra_all_vns, duplicate_ips, 'floating-ip', fip_uuids))

        num_addrs += len(fip_uuids)

        aip_rows = self.cassandra_xget(fq_name_table, 'alias_ip')
        aip_uuids = [x.split(':')[-1] for x, _ in aip_rows]
        ret_errors.extend(self._addr_alloc_process_ip_objects(
            cassandra_all_vns, duplicate_ips, 'alias-ip', aip_uuids))

        num_addrs += len(aip_uuids)

        logger.debug("Got %d networks %d addresses",
                     len(cassandra_all_vns), num_addrs)

        return (zk_all_vns, cassandra_all_vns, duplicate_ips, ret_errors,
                stale_zk_path)

    def audit_orphan_resources(self):
        errors = []
        logger = self._logger

        logger.debug("Reading all objects from obj_uuid_table")
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        orphan_resources = {}
        for obj_uuid, _ in self.cassandra_get_range(obj_uuid_table, column_count=1):
            cols = dict(self.cassandra_xget(obj_uuid_table, obj_uuid))
            obj_type = json.loads(cols.get('type', '"UnknownType"'))
            if 'parent_type' not in cols:
                logger.debug("ignoring '%s' as not parent type", obj_type)
                continue
            parent_type = json.loads(cols['parent_type']).replace('-', '_')
            parent_uuid = None
            for col_name in cols:
                if col_name.startswith('parent:%s:' % parent_type):
                    parent_uuid = col_name.split(':')[-1]
                    break
            try:
                obj_uuid_table.get(parent_uuid)
            except (pycassa.NotFoundException, KeyError):
                msg = ("%s %s parent does not exists. Should be %s %s" %
                       (obj_type, obj_uuid, parent_type, parent_uuid))
                errors.append(OrphanResourceError(msg))
                orphan_resources.setdefault(obj_type, []).append(obj_uuid)

        return orphan_resources, errors

    def audit_route_targets_routing_instance_backrefs(self):
        # System RT can have more than one RI back-ref if it is a LR's RT
        # - one RI back-ref per LR VMI and the RI corresponds to the VMI's RT
        # - one RI back-ref to the dedicated left VN RI used for SNAT stuff
        #   if LR have a gateway
        # or the VN/RI it was allocated for is part of a service chain

        fq_name_table = self._cf_dict['obj_fq_name_table']
        uuid_table = self._cf_dict['obj_uuid_table']
        sc_ri_fields = ['prop:service_chain_information',
                        'prop:ipv6_service_chain_information']

        back_refs_to_remove = {}
        errors = []
        for fq_name_uuid_str, _ in self.cassandra_xget(fq_name_table, 'route_target'):
            fq_name_str, _, uuid = fq_name_uuid_str.rpartition(':')
            try:
                asn, id = _parse_rt(fq_name_str)
            except ValueError:
                continue
            if (asn != self.global_asn or
                    id < get_bgp_rtgt_min_id(self.global_asn)):
                continue  # Ignore user defined RT
            try:
                cols = self.cassandra_xget(uuid_table, uuid, column_start='backref:',
                                       column_finish='backref;')
            except pycassa.NotFoundException:
                continue
            id_str = "%(#)010d" % {'#': id}
            rt_zk_path = os.path.join(self.base_rtgt_id_zk_path, id_str)
            try:
                zk_fq_name_str = self.zk_get(rt_zk_path)[0]
            except kazoo.exceptions.NoNodeError:
                msg = ("Cannot read zookeeper RT ID %s for RT %s(%s)" %
                       (rt_zk_path, fq_name_str, uuid))
                self._logger.warning(msg)
                errors.append(RTbackrefError(msg))
                continue
            lr_uuids = []
            ri_uuids = []
            for col, _ in cols:
                if col.startswith('backref:logical_router:'):
                    lr_uuids.append(col.rpartition(':')[-1])
                elif col.startswith('backref:routing_instance:'):
                    ri_uuids.append(col.rpartition(':')[-1])

            if len(lr_uuids) > 1:
                msg = ("RT %s(%s) have more than one LR: %s" %
                       (fq_name_str, uuid, ', '.join(lr_uuids)))
                self._logger.warning(msg)
                errors.append(RTbackrefError(msg))
                continue
            elif not lr_uuids and len(ri_uuids) >= 1:
                # Not LR's RT, so need to clean stale RI back-ref
                # just keep back-ref to RI pointed by zookeeper
                # if the VN/RI is not part to a service chain
                try:
                    zk_ri_fq_name_uuid_str = self.cassandra_get(
                        fq_name_table,
                        'routing_instance',
                        column_start='%s:' % zk_fq_name_str,
                        column_finish='%s;' % zk_fq_name_str,
                    )
                except pycassa.NotFoundException:
                    continue
                zk_ri_fq_name_str, _, zk_ri_uuid = zk_ri_fq_name_uuid_str.\
                    popitem()[0].rpartition(':')
                try:
                    # TODO(ethuleau): check import and export
                    ri_uuids.remove(zk_ri_uuid)
                except ValueError:
                    # TODO(ethuleau): propose a heal method to add
                    #                 missing RI back-refs
                    msg = ("RT %s(%s) has no back-ref to the RI pointed by "
                           "zookeeper %s(%s)" % (fq_name_str, uuid,
                                                 zk_fq_name_str, zk_ri_uuid))
                    self._logger.warning(msg)
                    errors.append(RTbackrefError(msg))
                for ri_uuid in ri_uuids[:]:
                    try:
                        ri_cols = self.cassandra_get(
                            uuid_table,
                            ri_uuid, columns=['fq_name'] + sc_ri_fields)
                    except pycassa.NotFoundException:
                        msg = ("Cannot read from cassandra RI %s of RT %s(%s)"
                               % (ri_uuid, fq_name_str, uuid))
                        self._logger.warning(msg)
                        errors.append(RTbackrefError(msg))
                        continue
                    ri_fq_name = json.loads(ri_cols['fq_name'])
                    is_ri_sc = (any(c in list(ri_cols.keys()) and ri_cols[c] for c in sc_ri_fields) and
                                ri_fq_name[-1].startswith('service-'))
                    if (is_ri_sc and
                            ri_fq_name[:-1] ==
                            zk_ri_fq_name_str.split(':')[:-1]):
                        # TODO(ethuleau): check corresponding SI and also if it
                        #                 is exported only
                        ri_uuids.remove(ri_uuid)

            elif len(lr_uuids) == 1:
                lr_uuid = lr_uuids[0]
                # check zookeeper pointed to that LR
                try:
                    lr_cols = dict(self.cassandra_xget(uuid_table, lr_uuid))
                except pycassa.NotFoundException:
                    msg = ("Cannot read from cassandra LR %s back-referenced "
                           "by RT %s(%s) in zookeeper" %
                           (lr_uuid, fq_name_str, uuid))
                    self._logger.warning(msg)
                    errors.append(RTbackrefError(msg))
                    continue
                lr_fq_name = ':'.join(json.loads(lr_cols['fq_name']))
                if zk_fq_name_str != lr_fq_name:
                    msg = ("LR %s(%s) back-referenced does not correspond to "
                           "the LR pointed by zookeeper %s for RT %s(%s)" %
                           (lr_fq_name, lr_uuid, zk_fq_name_str, fq_name_str,
                            uuid))
                    self._logger.warning(msg)
                    errors.append(RTbackrefError(msg))
                    continue
                # check RI back-refs correspond to LR VMIs
                vmi_ris = []
                for col, _ in list(lr_cols.items()):
                    if col.startswith('ref:service_instance:'):
                        # if LR have gateway and SNAT, RT have back-ref to SNAT
                        # left VN's RI (only import LR's RT to that RI)
                        si_uuid = col.rpartition(':')[-1]
                        try:
                            si_cols = uuid_table.get(
                                si_uuid, columns=['fq_name'])
                        except pycassa.NotFoundException:
                            msg = ("Cannot read from cassandra SI %s of LR "
                                   "%s(%s) of RT %s(%s)" %
                                   (si_uuid, lr_fq_name, lr_uuid, fq_name_str,
                                    uuid))
                            self._logger.warning(msg)
                            errors.append(RTbackrefError(msg))
                            continue
                        si_fq_name = json.loads(si_cols['fq_name'])
                        snat_ri_name = '%s_%s' % (
                            _VN_SNAT_PREFIX_NAME, si_fq_name[-1])
                        snat_ri_fq_name = si_fq_name[:-1] + 2 * [snat_ri_name]
                        snat_ri_fq_name_str = ':'.join(snat_ri_fq_name)
                        try:
                            snat_ri_fq_name_uuid_str = self.cassandra_get(
                                fq_name_table,
                                'routing_instance',
                                column_start='%s:' % snat_ri_fq_name_str,
                                column_finish='%s;' % snat_ri_fq_name_str,
                            )
                        except pycassa.NotFoundException:
                            msg = ("Cannot read from cassandra SNAT RI %s of "
                                   "LR %s(%s) of RT %s(%s)" %
                                   (snat_ri_fq_name_str, lr_fq_name, lr_uuid,
                                    fq_name_str, uuid))
                            self._logger.warning(msg)
                            errors.append(RTbackrefError(msg))
                            continue
                        snat_ri_uuid = snat_ri_fq_name_uuid_str.popitem()[
                            0].rpartition(':')[-1]
                        try:
                            self.cassandra_remove(ri_uuids, snat_ri_uuid)
                            # TODO(ethuleau): check only import
                        except ValueError:
                            # TODO(ethuleau): propose a heal method to add
                            #                 missing RI back-refs
                            msg = ("RT %s(%s) has no back-ref to the SNAT RI "
                                   "%s(%s) allocated for LR's %s(%s)" %
                                   (fq_name_str, uuid, snat_ri_fq_name_str,
                                    snat_ri_uuid, lr_fq_name, lr_uuid))
                            self._logger.warning(msg)
                            errors.append(RTbackrefError(msg))
                        continue
                    elif not col.startswith('ref:virtual_machine_interface:'):
                        continue
                    vmi_uuid = col.rpartition(':')[-1]
                    try:
                        vmi_cols = self.cassandra_xget(
                            uuid_table,
                            vmi_uuid,
                            column_start='ref:routing_instance:',
                            column_finish='ref:routing_instance;')
                    except pycassa.NotFoundException:
                        msg = ("Cannot read from cassandra VMI %s of LR "
                               "%s(%s) of RT %s(%s)" %
                               (vmi_uuid, lr_fq_name, lr_uuid, fq_name_str,
                                uuid))
                        self._logger.warning(msg)
                        errors.append(RTbackrefError(msg))
                        continue
                    for col, _ in vmi_cols:
                        vmi_ri_uuid = col.rpartition(':')[-1]
                        try:
                            # TODO(ethuleau): check import and export
                            self.cassandra_remove(ri_uuids, vmi_ri_uuid)
                        except ValueError:
                            # TODO(ethuleau): propose a heal method to add
                            #                 missing RI back-refs
                            msg = ("RT %s(%s) has no back-ref to the RI "
                                   "pointed by LR's %s(%s) VMI %s" %
                                   (fq_name_str, uuid, lr_fq_name, lr_uuid,
                                    vmi_uuid))
                            self._logger.warning(msg)
                            errors.append(RTbackrefError(msg))
            if ri_uuids:
                back_refs_to_remove[(fq_name_str, uuid)] = ri_uuids

        return errors, back_refs_to_remove

class DatabaseChecker(DatabaseManager):
    def checker(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('(v%s) Checker %s: Success', __version__,
                                      func.__name__)
                else:
                    self._logger.error(
                        '(v%s) Checker %s: Failed:\n%s\n',
                        __version__,
                        func.__name__,
                        '\n'.join(e.msg for e in errors)
                    )
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('(v%s) Checker %s: Exception, %s',
                                       __version__, func.__name__, err_msg)
                raise
        # end wrapper

        wrapper.__dict__['is_checker'] = True
        return wrapper
    # end checker

    @checker
    def check_zk_mode_and_node_count(self):
        """Displays error info about broken zk cluster."""
        ret_errors = []
        stats = {}
        modes = {}
        modes['leader'] = 0
        modes['follower'] = 0
        modes['standalone'] = 0
        # Collect stats
        for server in self._api_args.zk_server_ip.split(','):
            try:
                zk_client = kazoo.client.KazooClient(server)
                zk_client.start()
                self._logger.debug("Issuing 'stat' on %s: ", server)
                stat_out = zk_client.command('stat')
                self._logger.debug("Got: %s" % (stat_out))
                zk_client.stop()
                stats[server] = stat_out
            except Exception as e:
                msg = "Cannot get stats on zk node %s: %s" % (server, str(e))
                ret_errors.append(ZkStandaloneError(msg))

        # Check mode
        for stat_out in list(stats.values()):
            mode = re.search('Mode:(.*)\n', stat_out).group(1).strip()
            modes[mode] += 1
        n_zk_servers = len(self._api_args.zk_server_ip.split(','))
        if n_zk_servers == 1:
            # good-case: 1 node in standalone
            if not modes['standalone'] == 1:
                err_msg = "Error, Single zookeeper server and modes %s." \
                          % (str(modes))
                ret_errors.append(ZkStandaloneError(err_msg))
        else:
            # good-case: 1 node in leader, >=1 in followers
            if (modes['leader'] == 1) and (modes['follower'] >= 1):
                pass  # ok
            else:
                ret_errors.append(ZkFollowersError(
                    "Error, Incorrect modes %s." % (str(modes))))

        # Check node count
        node_counts = []
        for stat_out in list(stats.values()):
            nc = int(re.search('Node count:(.*)\n', stat_out).group(1))
            node_counts.append(nc)
        # all nodes should have same count, so set should have 1 elem
        if len(set(node_counts)) != 1:
            ret_errors.append(ZkNodeCountsError(
                "Error, Differing node counts %s." % (str(node_counts))))

        return ret_errors
    # end check_zk_mode_and_node_count

    @checker
    def check_cassandra_keyspace_replication(self):
        """Displays error info about wrong replication factor in Cassandra."""
        ret_errors = []
        logger = self._logger
        socket_factory = pycassa.connection.default_socket_factory
        if ('cassandra_use_ssl' in self._api_args and
                self._api_args.cassandra_use_ssl):
            socket_factory = self._make_ssl_socket_factory(
                    self._api_args.cassandra_ca_certs, validate=False)
        for server in self._cassandra_servers:
            try:
                sys_mgr = pycassa.SystemManager(server,
                                    socket_factory=socket_factory,
                                    credentials=self.creds)
            except Exception as e:
                msg = "Cannot connect to cassandra node %s: %s" % (server,
                                                                   str(e))
                ret_errors.append(CassWrongRFError(msg))
                continue

            for ks_name, _ in self._db_info:
                if self._api_args.cluster_id:
                    full_ks_name = '%s_%s' % (
                        self._api_args.cluster_id, ks_name)
                else:
                    full_ks_name = ks_name
                logger.debug("Reading keyspace properties for %s on %s: ",
                             ks_name, server)
                ks_prop = sys_mgr.get_keyspace_properties(full_ks_name)
                logger.debug("Got %s", ks_prop)

                repl_factor = int(
                    ks_prop['strategy_options']['replication_factor'])
                if (repl_factor != len(self._cassandra_servers)):
                    errmsg = 'Incorrect replication factor %d for keyspace %s'\
                             % (repl_factor, ks_name)
                    ret_errors.append(CassWrongRFError(errmsg))

        return ret_errors
    # end check_cassandra_keyspace_replication

    def check_rabbitmq_queue(self):
        pass
    # end check_rabbitmq_queue

    @checker
    def check_fq_name_uuid_match(self):
        """Displays mismatch between obj-fq-name-table and obj-uuid-table
        in Cassandra."""
        # ensure items in obj-fq-name-table match to obj-uuid-table
        ret_errors = []
        logger = self._logger

        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        fq_name_table_all = []
        logger.debug("Reading all objects from obj_fq_name_table")
        for obj_type, _ in self.cassandra_get_range(obj_fq_name_table, column_count=1):
            for fq_name_str_uuid, _ in self.cassandra_xget(obj_fq_name_table, obj_type):
                fq_name_str = ':'.join(fq_name_str_uuid.split(':')[:-1])
                fq_name_str = cfgm_common.utils.decode_string(fq_name_str)
                obj_uuid = fq_name_str_uuid.split(':')[-1]
                fq_name_table_all.append((obj_type, fq_name_str, obj_uuid))
                try:
                    obj_cols = self.cassandra_get(obj_uuid_table, obj_uuid,
                                                  columns=['fq_name'])
                except (pycassa.NotFoundException, KeyError):
                    ret_errors.append(FQNStaleIndexError(
                        'Missing object %s %s %s in uuid table'
                        % (obj_uuid, obj_type, fq_name_str)))
                    continue
                obj_fq_name_str = ':'.join(json.loads(obj_cols['fq_name']))
                obj_fq_name_str = cfgm_common.utils.decode_string(obj_fq_name_str)
                if fq_name_str != obj_fq_name_str:
                    ret_errors.append(FQNMismatchError(
                        'Mismatched FQ Name %s (index) vs %s (object)'
                        % (fq_name_str, obj_fq_name_str)))
            # end for all objects in a type
        # end for all obj types
        logger.debug("Got %d objects", len(fq_name_table_all))

        uuid_table_all = []
        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, _ in self.cassandra_get_range(obj_uuid_table, column_count=1):
            try:
                cols = self.cassandra_get(obj_uuid_table,
                    obj_uuid, columns=['type', 'fq_name'])
            except (pycassa.NotFoundException, KeyError):
                msg = ("'type' and/or 'fq_name' properties of '%s' missing" %
                       obj_uuid)
                ret_errors.append(MandatoryFieldsMissingError(msg))
                continue
            obj_type = json.loads(cols['type'])
            fq_name_str = ':'.join(json.loads(cols['fq_name']))
            uuid_table_all.append((obj_type, fq_name_str, obj_uuid))

        logger.debug("Got %d objects", len(uuid_table_all))

        for extra in set(fq_name_table_all) - set(uuid_table_all):
            obj_type, fq_name_str, obj_uuid = extra
            ret_errors.append(FQNStaleIndexError(
                'Stale index %s %s %s in obj_fq_name_table'
                % (obj_type, fq_name_str, obj_uuid)))

        for extra in set(uuid_table_all) - set(fq_name_table_all):
            obj_type, fq_name_str, obj_uuid = extra
            ret_errors.append(FQNIndexMissingError(
                'Extra object %s %s %s in obj_uuid_table'
                % (obj_type, fq_name_str, obj_uuid)))

        return ret_errors
    # end check_fq_name_uuid_match

    @checker
    def check_duplicate_fq_name(self):
        logger = self._logger
        errors = []
        fq_name_table = self._cf_dict['obj_fq_name_table']
        uuid_table = self._cf_dict['obj_uuid_table']

        logger.debug("Reading all objects from obj_fq_name_table")
        logger.warning("Be careful, that check can return false positive "
                       "errors if stale FQ names and stale resources were not "
                       "cleaned before. Run at least commands "
                       "'clean_obj_missing_mandatory_fields', "
                       "'clean_orphan_resources' and 'clean_stale_fq_names' "
                       "before.")
        resource_map = {}
        stale_fq_names = set([])
        for obj_type, _ in self.cassandra_get_range(fq_name_table, column_count=1):
            for fq_name_str_uuid, _ in self.cassandra_xget(fq_name_table, obj_type):
                fq_name_str, _, uuid = fq_name_str_uuid.rpartition(':')
                try:
                    obj = self.cassandra_get(uuid_table, uuid, columns=['prop:id_perms'])
                    created_at = json.loads(obj['prop:id_perms']).get(
                        'created', 'unknown')
                    resource_map.setdefault(obj_type, {}).setdefault(
                        fq_name_str, set([])).add((uuid, created_at))
                except (pycassa.NotFoundException, KeyError):
                    stale_fq_names.add(fq_name_str)
        if stale_fq_names:
            logger.info("Found stale fq_name index entry: %s. Use "
                        "'clean_stale_fq_names' commands to repair that. "
                        "Ignore it", ', '.join(stale_fq_names))

        for type, type_map in list(resource_map.items()):
            for fq_name_str, uuids in list(type_map.items()):
                if len(uuids) != 1:
                    msg = ("%s with FQ name '%s' is used by %d different "
                           "objects: %s" % (
                               type.replace('_', ' ').title(),
                               fq_name_str,
                               len(uuids),
                               ', '.join(['%s (created at %s)' % (u, c)
                                          for u, c in uuids]),
                           ))
                    errors.append(FqNameDuplicateError(msg))

        return errors

    @checker
    def check_obj_mandatory_fields(self):
        """Displays missing mandatory fields of objects at obj-uuid-table
        in Cassandra."""
        # ensure fq_name, type, uuid etc. exist
        ret_errors = []
        logger = self._logger

        logger.debug("Reading all objects from obj_uuid_table")
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        num_objs = 0
        num_bad_objs = 0
        for obj_uuid, _ in self.cassandra_get_range(obj_uuid_table, column_count=1):
            cols = dict(self.cassandra_xget(obj_uuid_table, obj_uuid))
            num_objs += 1
            for col_name in self.OBJ_MANDATORY_COLUMNS:
                if col_name in cols:
                    continue
                num_bad_objs += 1

                ret_errors.append(MandatoryFieldsMissingError(
                    'Error, obj %s missing column %s' % (obj_uuid, col_name)))

        logger.debug("Got %d objects %d with missing mandatory fields",
                     num_objs, num_bad_objs)

        return ret_errors
    # end check_obj_mandatory_fields

    @checker
    def check_subnet_uuid(self):
        """Displays inconsistencies between useragent/obj-uuid-table for
        subnets, subnet uuids in Cassandra."""
        # whether useragent subnet uuid and uuid in subnet property match
        ret_errors = []

        ua_subnet_info, vnc_subnet_info, errors = self.audit_subnet_uuid()
        ret_errors.extend(errors)

        # check #subnets in useragent table vs #subnets in obj_uuid_table
        if len(list(ua_subnet_info.keys())) != len(list(vnc_subnet_info.keys())):
            ret_errors.append(SubnetCountMismatchError(
                "Mismatch #subnets useragent %d #subnets ipam-subnet %d"
                % (len(list(ua_subnet_info.keys())), len(list(vnc_subnet_info.keys())))))

        # check if subnet-uuids match in useragent table vs obj_uuid_table
        extra_ua_subnets = set(ua_subnet_info.keys()) - set(
            vnc_subnet_info.keys())
        if extra_ua_subnets:
            ret_errors.append(UseragentSubnetExtraError(
                "Extra useragent subnets %s" % (str(extra_ua_subnets))))

        extra_vnc_subnets = set(vnc_subnet_info.keys()) - set(
            ua_subnet_info.keys())
        if extra_vnc_subnets:
            ret_errors.append(UseragentSubnetMissingError(
                "Missing useragent subnets %s" % (extra_vnc_subnets)))

        return ret_errors
    # end check_subnet_uuid

    @checker
    def check_subnet_addr_alloc(self):
        """Displays inconsistencies between zk and cassandra for Ip's, Subnets
        and VN's."""
        # whether ip allocated in subnet in zk match iip+fip in cassandra
        (zk_all_vns, cassandra_all_vns, duplicate_ips,
            ret_errors, stale_zk_path) = self.audit_subnet_addr_alloc()

        # check stale ZK subnet path
        for path in stale_zk_path:
            msg = ("ZK subnet path '%s' does ends with a valid IP network"
                   % path)
            ret_errors.append(ZkSubnetPathInvalid(msg))

        # check for differences in networks
        extra_vns = set(zk_all_vns.keys()) - set(cassandra_all_vns.keys())
        # for a flat-network, cassandra has VN-ID as key while ZK has subnet
        # Are these extra VNs are due to flat-subnet?
        if extra_vns:
            for sn in extra_vns:
                # ensure Subnet is found ZK is empty
                if list(filter(bool, list(zk_all_vns[sn].values()))):
                    errmsg = 'Extra VN in zookeeper (vs. cassandra) for %s' \
                         % (str(sn))
                    ret_errors.append(ZkVNExtraError(errmsg))

        extra_vn = set()
        # Subnet lock path is not created until an IP is allocated into it
        for vn_key in set(cassandra_all_vns.keys()) - set(zk_all_vns.keys()):
            for sn_key, addrs in list(cassandra_all_vns[vn_key].items()):
                if addrs['addrs']:
                    extra_vn.add(vn_key)
        if extra_vn:
            errmsg = 'Missing VN in zookeeper (vs.cassandra) for %s' \
                     % (str(extra_vn))
            ret_errors.append(ZkVNMissingError(errmsg))

        # check for differences in subnets
        zk_all_vn_sn = []
        for vn_key, vn in list(zk_all_vns.items()):
            zk_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])

        cassandra_all_vn_sn = []
        # ignore subnet without address and not lock in zk
        for vn_key, vn in list(cassandra_all_vns.items()):
            for sn_key, addrs in list(vn.items()):
                if not addrs['addrs']:
                    if (vn_key not in zk_all_vns or
                            sn_key not in zk_all_vns[vn_key]):
                        continue
                cassandra_all_vn_sn.extend([(vn_key, sn_key)])

        extra_vn_sns = set(zk_all_vn_sn) - set(cassandra_all_vn_sn)
        for extra_vn_sn in extra_vn_sns:
            # ensure the Subnet is found in Cassandra when
            # ZK keys and Cassandra keys mismatch
            if not any([extra_vn_sn[1] in sdict
                       for sdict in list(cassandra_all_vns.values())]):
                errmsg = 'Extra VN/SN in zookeeper for %s' % (extra_vn_sn,)
                ret_errors.append(ZkSubnetExtraError(errmsg))

        extra_vn_sn = set(cassandra_all_vn_sn) - set(zk_all_vn_sn)
        if extra_vn_sn:
            errmsg = 'Missing VN/SN in zookeeper for %s' % (extra_vn_sn)
            ret_errors.append(ZkSubnetMissingError(errmsg))

        # Duplicate IPs
        for vn_key, vn in list(duplicate_ips.items()):
            for sn_key, subnet in list(vn.items()):
                for ip_addr, iip_uuids in list(subnet.items()):
                    cols = self.cassandra_get(self._cf_dict['obj_uuid_table'],
                        iip_uuids[0], columns=['type'])
                    type = json.loads(cols['type'])
                    msg = ("%s %s from VN %s and subnet %s is duplicated: %s" %
                           (type.replace('_', ' ').title(),
                            ip_addr, vn_key, sn_key, iip_uuids))
                    ret_errors.append(IpAddressDuplicateError(msg))
        # check for differences in ip addresses
        for vn, sn_key in set(zk_all_vn_sn) & set(cassandra_all_vn_sn):
            sn_start = cassandra_all_vns[vn][sn_key]['start']
            sn_gw_ip = cassandra_all_vns[vn][sn_key]['gw']
            sn_dns = cassandra_all_vns[vn][sn_key]['dns']
            zk_ips = zk_all_vns[vn][sn_key]
            cassandra_ips = cassandra_all_vns[vn][sn_key]['addrs']
            extra_ips = set(zk_ips) - set(cassandra_ips)
            for iip_uuid, ip_addr in extra_ips:
                # ignore network, bcast and gateway ips
                if (IPAddress(ip_addr) == IPNetwork(sn_key).network or
                        IPAddress(ip_addr) == IPNetwork(sn_key).broadcast):
                    continue
                if (ip_addr == sn_gw_ip or ip_addr == sn_dns or
                        ip_addr == sn_start):
                    continue

                errmsg = ('Extra IP %s (IIP %s) in zookeeper for vn %s' %
                          (ip_addr, iip_uuid, vn))
                ret_errors.append(ZkIpExtraError(errmsg))
            # end all zk extra ips

            extra_ips = set(cassandra_ips) - set(zk_ips)
            for iip_uuid, ip_addr in extra_ips:
                errmsg = ('Missing IP %s (IIP %s) in zookeeper for vn %s' %
                          (ip_addr, iip_uuid, vn))
                ret_errors.append(ZkIpMissingError(errmsg))
            # end all cassandra extra ips
        # for all common VN/subnets
        return ret_errors
    # end check_subnet_addr_alloc

    @checker
    def check_route_targets_id(self):
        """Displays route targets ID inconsistencies between zk and cassandra.

        Route target IDs locked in the ZK database are considered as the source
        of trust. Config and Schema DB tables will be updated accordingly.
        Then in different cases, the ZK stale lock will be cleaned.
        """
        ret_errors = []
        logger = self._logger
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        zk_set, schema_set, config_set, malformed_set, _, errors =\
            self.audit_route_targets_id()
        ret_errors.extend(errors)

        for fq_name_str, uuid in malformed_set:
            msg = ("The Route Target '%s' (%s) is malformed" %
                   (fq_name_str, uuid))
            ret_errors.append(RTMalformedError(msg))

        extra = dict()
        [extra.setdefault(id, set()).add(fq) for id, fq in schema_set - zk_set]
        for id, res_fq_name_strs in list(extra.items()):
            msg = ("Extra Route Target ID in schema DB for ID %d, used by: "
                   "%s" % (id, ', '.join(res_fq_name_strs)))
            ret_errors.append(SchemaRTgtIdExtraError(msg))

        extra = dict()
        [extra.setdefault(id, set()).add(fq) for id, fq in config_set - zk_set]
        for id, res_fq_name_strs in list(extra.items()):
            msg = ("Extra Route Target ID in API server DB for ID %d, used "
                   "by: %s" % (id, ', '.join(res_fq_name_strs)))
            ret_errors.append(ConfigRTgtIdExtraError(msg))

        for id, res_fq_name_str in self.get_stale_zk_rt(
                zk_set, schema_set, config_set):
            msg = ("Extra route target ID in zookeeper for ID %s, used by: %s"
                   % (id, res_fq_name_str))
            ret_errors.append(ZkRTgtIdExtraError(msg))

        return ret_errors
    # end check_route_targets_id

    @checker
    def check_virtual_networks_id(self):
        """Displays VN ID inconsistencies between zk and cassandra."""
        ret_errors = []

        zk_set, cassandra_set, errors, duplicate_ids, missing_ids =\
            self.audit_virtual_networks_id()
        ret_errors.extend(errors)

        for vn_id, fq_name_uuids in list(duplicate_ids.items()):
            msg = "VN ID %s is duplicated between %s" % (vn_id, fq_name_uuids)
            ret_errors.append(VNDuplicateIdError(msg))

        extra_vn_ids = zk_set - cassandra_set
        for vn_id, vn_fq_name_str in extra_vn_ids:
            errmsg = ('Extra VN IDs in zookeeper for vn %s %s' %
                      (vn_fq_name_str, vn_id))
            ret_errors.append(ZkVNIdExtraError(errmsg))

        extra_vn_ids = cassandra_set - zk_set
        for vn_id, vn_fq_name_str in extra_vn_ids:
            errmsg = ('Missing VN IDs in zookeeper for vn %s %s' %
                      (vn_fq_name_str, vn_id))
            ret_errors.append(ZkVNIdMissingError(errmsg))

        if missing_ids:
            msg = 'Missing VN IDs in cassandra for vn %s' % missing_ids
            ret_errors.append(VirtualNetworkIdMissingError(msg))

        return ret_errors
    # end check_virtual_networks_id

    @checker
    def check_security_groups_id(self):
        """Displays Security group ID inconsistencies between zk and cassandra.
        """
        ret_errors = []

        zk_set, cassandra_set, errors, duplicate_ids, missing_ids =\
            self.audit_security_groups_id()
        ret_errors.extend(errors)

        for sg_id, fq_name_uuids in list(duplicate_ids.items()):
            msg = "SG ID %s is duplicated between %s" % (sg_id, fq_name_uuids)
            ret_errors.append(SGDuplicateIdError(msg))

        extra_sg_ids = zk_set - cassandra_set
        for sg_id, sg_fq_name_str in extra_sg_ids:
            errmsg = ('Extra SG IDs in zookeeper for sg %s %s' %
                      (sg_fq_name_str, sg_id))
            ret_errors.append(ZkSGIdExtraError(errmsg))

        extra_sg_ids = cassandra_set - zk_set
        for sg_id, sg_fq_name_str in extra_sg_ids:
            errmsg = ('Missing SG IDs in zookeeper for sg %s %s' %
                      (sg_fq_name_str, sg_id))
            ret_errors.append(ZkSGIdMissingError(errmsg))

        if missing_ids:
            msg = 'Missing SG IDs in cassandra for sg %s' % missing_ids
            ret_errors.append(VirtualNetworkIdMissingError(msg))

        return ret_errors
    # end check_security_groups_id

    @checker
    def check_orphan_resources(self):
        """Displays orphaned entries in obj_uuid_table of cassandra."""
        _, errors = self.audit_orphan_resources()
        return errors

    def check_schema_db_mismatch(self):
        # TODO detect all objects persisted that have discrepancy from
        # defined schema
        pass

    @checker
    def check_route_targets_routing_instance_backrefs(self):
        errors, back_refs_to_remove =\
            self.audit_route_targets_routing_instance_backrefs()
        for (rt_fq_name_str, rt_uuid), ri_uuids in list(back_refs_to_remove.items()):
            msg = ("Extra RI back-ref(s) %s from RT %s(%s)" %
                   (', '.join(ri_uuids), rt_fq_name_str, rt_uuid))
            errors.append(RTbackrefError(msg))
        return errors

class DatabaseCleaner(DatabaseManager):
    def cleaner(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('(v%s) Cleaner %s: Success', __version__,
                                      func.__name__)
                else:
                    self._logger.error(
                        '(v%s) Cleaner %s: Failed:\n%s\n',
                        __version__,
                        func.__name__, '\n'.join(e.msg for e in errors),
                    )
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('(v%s) Cleaner %s: Exception, %s',
                                       __version__, func.__name__, err_msg)
                raise
        # end wrapper

        wrapper.__dict__['is_cleaner'] = True
        return wrapper
    # end cleaner

    def _remove_config_object(self, type, uuids):
        uuid_table = self._cf_dict['obj_uuid_table']
        for uuid in uuids:
            try:
                cols = self.cassandra_get(uuid_table, uuid, column_start='backref:',
                                      column_finish='backref;')
                for backref_str in list(cols.keys()):
                    backref_uuid = backref_str.rpartition(':')[-1]
                    ref_str = 'ref:%s:%s' % (type, uuid)
                    try:
                        self.cassandra_remove(uuid_table, backref_uuid, columns=[ref_str])
                    except (pycassa.NotFoundException, KeyError):
                        continue
            except (pycassa.NotFoundException, KeyError):
                pass
            try:
                self.cassandra_remove(uuid_table, uuid)
            except (pycassa.NotFoundException, KeyError):
                continue

    @cleaner
    def clean_stale_fq_names(self, res_type=None):
        """Removes stale entries from obj_fq_name_table of cassandra."""
        logger = self._logger
        ret_errors = []

        obj_fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_fq_name_table")
        if res_type is not None:
            iterator = ((type, None) for type in set(res_type))
        else:
            iterator = self.cassandra_get_range(obj_fq_name_table, column_count=1)
        for obj_type, _ in iterator:
            stale_cols = []
            for fq_name_str_uuid, _ in self.cassandra_xget(obj_fq_name_table, obj_type):
                obj_uuid = fq_name_str_uuid.split(':')[-1]
                try:
                    obj_uuid_table.get(obj_uuid)
                except (KeyError, pycassa.NotFoundException):
                    logger.info("Found stale fq_name index entry: %s",
                                fq_name_str_uuid)
                    stale_cols.append(fq_name_str_uuid)

            if stale_cols:
                if not self._args.execute:
                    logger.info("Would removed stale %s fq_names: %s",
                                obj_type, stale_cols)
                else:
                    logger.info("Removing stale %s fq_names: %s", obj_type,
                                stale_cols)
                    obj_fq_name_table.remove(obj_type, columns=stale_cols)

        # TODO do same for zookeeper
        return ret_errors
    # end clean_stale_fq_names

    @cleaner
    def clean_stale_object(self):
        """Delete object in obj_uuid_table of cassandra for object with FQ name
        not correctly indexed by obj_fq_name_table."""
        logger = self._logger
        errors = []

        fq_name_table = self._cf_dict['obj_fq_name_table']
        uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_uuid_table")
        # dict of set, key is row-key val is set of col-names
        fixups = {}
        for uuid, cols in self.cassandra_get_range(uuid_table, columns=['type', 'fq_name']):
            type = json.loads(cols.get('type', ""))
            fq_name = json.loads(cols.get('fq_name', ""))
            if not type:
                logger.info("Unknown 'type' for object %s", uuid)
                continue
            if not fq_name:
                logger.info("Unknown 'fq_name' for object %s", uuid)
                continue
            fq_name_str = ':'.join(fq_name)
            try:
                self.cassandra_get(fq_name_table, type,
                                  columns=['%s:%s' % (fq_name_str, uuid)])
            except (pycassa.NotFoundException, KeyError):
                fixups.setdefault(type, {}).setdefault(
                    fq_name_str, set([])).add(uuid)

        for type, fq_name_uuids in list(fixups.items()):
            for fq_name_str, uuids in list(fq_name_uuids.items()):
                # Check fq_name already used
                try:
                    fq_name_uuid_str = self.cassandra_get(
                        fq_name_table,
                        type,
                        column_start='%s:' % fq_name_str,
                        column_finish='%s;' % fq_name_str,
                    )
                    fq_name_str, _, uuid = list(fq_name_uuid_str.keys())[0].\
                        rpartition(':')
                except (KeyError, pycassa.NotFoundException):
                    # fq_name index does not exists, need to be healed
                    continue
                # FQ name already there, check if it's a stale entry
                try:
                    self.cassandra_get(uuid_table, uuid, columns=['type'])
                    # FQ name already use by an object, remove stale object
                    if not self._args.execute:
                        logger.info("Would remove %s object(s) which share FQ "
                                    "name '%s' used by '%s': %s",
                                    type.replace('_', ' ').title(),
                                    fq_name_str, uuid,
                                    ', '.join(uuids))
                    else:
                        logger.info("Removing %s object(s) which share FQ "
                                    "name '%s' used by '%s': %s",
                                    type.replace('_', ' ').title(),
                                    fq_name_str, uuid,
                                    ', '.join(uuids))
                        if self.using_json is True:
                            [self.cassandra_remove(uuid_table, uuid) for uuid in uuids]
                        else:
                            bch = uuid_table.batch()
                            [bch.remove(uuid) for uuid in uuids]
                            bch.send()
                except (pycassa.NotFoundException, KeyError):
                    msg = ("Stale FQ name entry '%s', please run "
                           "'clean_stale_fq_names' before trying to clean "
                           "objects" % fq_name_str)
                    logger.warning(msg)
                    continue

        return errors

    @cleaner
    def clean_stale_back_refs(self):
        """Removes stale backref entries from obj_uuid_table of cassandra."""
        return self._remove_stale_from_uuid_table('backref')
    # end clean_stale_back_refs

    @cleaner
    def clean_stale_refs(self):
        """Removes stale ref entries from obj_uuid_table of cassandra."""
        return self._remove_stale_from_uuid_table('ref')
    # end clean_stale_refs

    @cleaner
    def clean_stale_children(self):
        """Removes stale children entries from obj_uuid_table of cassandra."""
        return self._remove_stale_from_uuid_table('children')
    # end clean_stale_back_refs

    @cleaner
    def clean_obj_missing_mandatory_fields(self):
        """Removes stale resources which are missing mandatory fields from
        obj_uuid_table of cassandra."""
        logger = self._logger
        ret_errors = []
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, _ in self.cassandra_get_range(obj_uuid_table, column_count=1):
            cols = dict(self.cassandra_xget(obj_uuid_table, obj_uuid))
            missing_cols = set(self.OBJ_MANDATORY_COLUMNS) - set(cols.keys())
            if not missing_cols:
                continue
            logger.info("Found object %s with missing columns %s", obj_uuid,
                        missing_cols)
            if not self._args.execute:
                logger.info("Would removed object %s", obj_uuid)
            else:
                logger.info("Removing object %s", obj_uuid)
                obj_uuid_table.remove(obj_uuid)

        return ret_errors
    # end clean_obj_missing_mandatory_fields

    @cleaner
    def clean_vm_with_no_vmi(self):
        """Removes VM's without VMI from cassandra."""
        logger = self._logger
        ret_errors = []
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        stale_vm_uuids = []
        logger.debug("Reading all VMs from obj_uuid_table")
        for obj_uuid, _ in self.cassandra_get_range(obj_uuid_table, column_count=1):
            cols = dict(self.cassandra_xget(obj_uuid_table, obj_uuid))
            obj_type = json.loads(cols.get('type', '""'))
            if not obj_type or obj_type != 'virtual_machine':
                continue
            vm_uuid = obj_uuid
            col_names = list(cols.keys())
            if (any(['backref' in col_name for col_name in col_names]) or
                    any(['children' in col_name for col_name in col_names])):
                continue
            logger.info("Found stale VM %s columns %s", vm_uuid, col_names)
            stale_vm_uuids.append(vm_uuid)

        logger.debug("Total %s VMs with no VMIs", len(stale_vm_uuids))
        if stale_vm_uuids:
            for vm_uuid in stale_vm_uuids:
                if not self._args.execute:
                    logger.info("Would removed stale VM %s", vm_uuid)
                else:
                    logger.info("Removing stale VM %s", vm_uuid)
                    obj_uuid_table.remove(vm_uuid)
            self.clean_stale_fq_names(['virtual_machine'])

        return ret_errors
    # end clean_vm_with_no_vmi

    @cleaner
    def clean_stale_route_target_id(self):
        """Removes stale RT ID's from route_target_table of cassandra and zk.

        Route target IDs locked in the ZK database are considered as the source
        of trust. Config and Schema DB tables will be updated accordingly.
        Then in different cases, the ZK stale lock will be cleaned.
        """
        logger = self._logger
        ret_errors = []
        fq_name_table = self._cf_dict['obj_fq_name_table']
        uuid_table = self._cf_dict['obj_uuid_table']
        rt_table = self._cf_dict['route_target_table']

        zk_set, schema_set, config_set, malformed_set, stale_list, errors =\
            self.audit_route_targets_id()
        ret_errors.extend(errors)

        # Remove malformed RT from config and schema keyspaces
        for fq_name_str, uuid in malformed_set:
            fq_name_uuid_str = '%s:%s' % (fq_name_str, uuid)
            if not self._args.execute:
                logger.info("Would removed stale route target %s (%s) in API "
                            "server and schema cassandra keyspaces",
                            fq_name_str, uuid)
            else:
                logger.info("Removing stale route target %s (%s) in API "
                            "server and schema cassandra keyspaces",
                            fq_name_str, uuid)
                self._remove_config_object('route_target', [uuid])
                self.cassandra_remove(rt_table, fq_name_str)
                self.cassandra_remove(fq_name_table, 'route_target',
                                     columns=[fq_name_uuid_str])

        for (fq_name_str, uuid, list_name), stale_rts in list(stale_list.items()):
            try:
                cols = self.cassandra_get(uuid_table, uuid, columns=[list_name])
            except (pycassa.NotFoundException, KeyError):
                continue
            rts = set(json.loads(cols[list_name]).get('route_target', []))
            if not rts & stale_rts:
                continue
            if not self._args.execute:
                logger.info("Would removed stale route target(s) '%s' in the "
                            "%s of the Virtual Network or Logical Router "
                            "%s(%s)", ','.join(stale_rts),
                            list_name[5:].replace('_', ' '), fq_name_str, uuid)
            else:
                logger.info("Removing stale route target(s) '%s' in the %s of "
                            "the Virtual Network or Logical Router %s(%s)",
                            ','.join(stale_rts),
                            list_name[5:].replace('_', ' '), fq_name_str, uuid)
                if not rts - stale_rts:
                    self.cassandra_remove(uuid_table, uuid, columns=[list_name])
                else:
                    cols = {
                        list_name: json.dumps({
                            'route_target': list(rts - stale_rts),
                        }),
                    }
                    self.cassandra_insert(uuid_table, uuid, cols)
        # Remove extra RT in Schema DB
        for id, res_fq_name_str in schema_set - zk_set:
            if not self._args.execute:
                logger.info("Would removed stale route target %s in schema "
                            "cassandra keyspace", res_fq_name_str)
            else:
                logger.info("Removing stale route target %s in schema "
                            "cassandra keyspace", res_fq_name_str)
                self.cassandra_remove(rt_table, res_fq_name_str)

        # Remove extra RT in Config DB
        for id, _ in config_set - zk_set:
            fq_name_str = 'target:%d:%d' % (self.global_asn, id)
            try:
                cols = self.cassandra_get(
                    fq_name_table,
                    'route_target',
                    column_start='%s:' % fq_name_str,
                    column_finish='%s;' % fq_name_str,
                )
            except (pycassa.NotFoundException, KeyError):
                continue
            uuid = list(cols.keys())[0].rpartition(':')[-1]
            fq_name_uuid_str = '%s:%s' % (fq_name_str, uuid)
            if not self._args.execute:
                logger.info("Would removed stale route target %s (%s) in API "
                            "server cassandra keyspace ", fq_name_str, uuid)
            else:
                logger.info("Removing stale route target %s (%s) in API "
                            "server cassandra keyspace ", fq_name_str, uuid)
                self._remove_config_object('route_target', [uuid])
                self.cassandra_remove(fq_name_table, 'route_target',
                                     columns=[fq_name_uuid_str])

        stale_zk_entry = self.get_stale_zk_rt(zk_set, schema_set, config_set)
        self._clean_zk_id_allocation(
            self.base_rtgt_id_zk_path, set([]), stale_zk_entry)
        if stale_zk_entry & schema_set and not self._args.execute:
            logger.info("Would removed stale route target(s) in schema "
                        "cassandra keyspace: %s",
                        ', '.join([f for _, f in stale_zk_entry]))
        elif stale_zk_entry & schema_set:
            logger.info("Removing stale route target(s) in schema cassandra "
                        "keyspace: %s",
                        ', '.join([f for _, f in stale_zk_entry]))
            if self.using_json is True:
                [self.cassandra_remove(rt_table, key) for _, key in stale_zk_entry]
            else:
                bch = rt_table.batch()
                [bch.remove(key) for _, key in stale_zk_entry]
                bch.send()

        return ret_errors

    @cleaner
    def clean_stale_security_group_id(self):
        """Removes stale SG ID's from obj_uuid_table of cassandra and zk."""
        logger = self._logger
        ret_errors = []
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        zk_set, cassandra_set, errors, duplicate_ids, _ =\
            self.audit_security_groups_id()
        ret_errors.extend(errors)

        self._clean_zk_id_allocation(self.base_sg_id_zk_path,
                                     cassandra_set,
                                     zk_set)
        path = '%s/%%s' % self.base_sg_id_zk_path
        uuids_to_deallocate = set()
        for id, fq_name_uuids in list(duplicate_ids.items()):
            id_str = "%(#)010d" % {'#': id}
            try:
                zk_fq_name_str = self.zk_get(path % id_str)[0]
            except (kazoo.exceptions.NoNodeError, KeyError):
                zk_fq_name_str = None
            uuids_to_deallocate |= {uuid for fq_name_str, uuid in fq_name_uuids
                                    if fq_name_str != zk_fq_name_str}
        if not uuids_to_deallocate:
            return
        if not self._args.execute:
            logger.info("Would remove the security ID allocation to %d SG: %s",
                        len(uuids_to_deallocate), uuids_to_deallocate)
        else:
            logger.info("Removing the security ID allocation to %d SG: %s",
                        len(uuids_to_deallocate), uuids_to_deallocate)
            if self.using_json is True:
                [self.cassandra_remove(obj_uuid_table, uuid, columns=['prop:security_group_id'])
                        for uuid in uuids_to_deallocate]
            else:
                bch = obj_uuid_table.batch()
                [bch.remove(uuid, columns=['prop:security_group_id'])
                    for uuid in uuids_to_deallocate]
                bch.send()

        return ret_errors
    # end clean_stale_security_group_id

    @cleaner
    def clean_stale_virtual_network_id(self):
        """Removes stale VN ID's from obj_uuid_table of cassandra and zk."""
        logger = self._logger
        ret_errors = []
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        zk_set, cassandra_set, errors, duplicate_ids, _ =\
            self.audit_virtual_networks_id()
        ret_errors.extend(errors)

        self._clean_zk_id_allocation(self.base_vn_id_zk_path,
                                     cassandra_set,
                                     zk_set,
                                     id_oper='%%s - %d' % VN_ID_MIN_ALLOC)

        path = '%s/%%s' % self.base_vn_id_zk_path
        uuids_to_deallocate = set()
        for id, fq_name_uuids in list(duplicate_ids.items()):
            id_str = "%(#)010d" % {'#': id - VN_ID_MIN_ALLOC}
            try:
                zk_fq_name_str = self.zk_get(path % id_str)[0]
            except (kazoo.exceptions.NoNodeError, KeyError):
                zk_fq_name_str = None
            uuids_to_deallocate |= {uuid for fq_name_str, uuid in fq_name_uuids
                                    if fq_name_str != zk_fq_name_str}
        if not uuids_to_deallocate:
            return
        if not self._args.execute:
            logger.info("Would remove the virtual network ID allocation to %d "
                        "SG: %s", len(uuids_to_deallocate),
                        uuids_to_deallocate)
        else:
            logger.info("Removing the virtual network ID allocation to %d SG: "
                        "%s", len(uuids_to_deallocate), uuids_to_deallocate)
            if self.using_json is True:
                [self.cassandra_remove(obj_uuid_table, uuid, columns=['prop:virtual_network_network_id'])
                    for uuid in uuids_to_deallocate]
            else:
                bch = obj_uuid_table.batch()
                [bch.remove(uuid, columns=['prop:virtual_network_network_id'])
                    for uuid in uuids_to_deallocate]
                bch.send()
        return ret_errors
    # end clean_stale_virtual_network_id

    @cleaner
    def clean_stale_subnet_uuid(self):
        """Removes stale UUID's from useragent_keyval_table of cassandra."""
        logger = self._logger
        ret_errors = []

        ua_subnet_info, vnc_subnet_info, errors = self.audit_subnet_uuid()
        ret_errors.extend(errors)

        extra_ua_subnets = set(ua_subnet_info.keys()) - set(
            vnc_subnet_info.keys())
        ua_kv_cf = self._cf_dict['useragent_keyval_table']
        for subnet_uuid in extra_ua_subnets:
            subnet_key = ua_subnet_info[subnet_uuid]
            if not self._args.execute:
                logger.info("Would remove stale subnet uuid %s in useragent "
                            "keyspace", subnet_uuid)
                logger.info("Would remove stale subnet key %s in useragent "
                            "keyspace", subnet_key)
            else:
                logger.info("Removing stale subnet uuid %s in useragent "
                            "keyspace", subnet_uuid)
                self.cassandra_remove(ua_kv_cf, subnet_uuid)
                logger.info("Removing stale subnet key %s in useragent "
                            "keyspace", subnet_key)
                self.cassandra_remove(ua_kv_cf, subnet_key)

        # Since release 3.2, only subnet_id/subnet_key are store in
        # key/value store, the reverse was removed. Clean remaining key
        ua_kv_cf = self._cf_dict['useragent_keyval_table']
        stale_kv = set([])
        for key, cols in self.cassandra_get_range(ua_kv_cf):
            if self.KV_SUBNET_KEY_TO_UUID_MATCH.match(key):
                subnet_id = cols['value']
                try:
                    self.cassandra_get(ua_kv_cf, subnet_id)
                except (pycassa.NotFoundException, KeyError):
                    stale_kv.add(key)
        if stale_kv:
            if not self._args.execute:
                logger.info("Would remove stale subnet keys: %s", stale_kv)
            else:
                logger.info("Removing stale subnet keys: %s", stale_kv)
                if self.using_json is True:
                    [self.cassandra_remove(ua_kv_cf, key) for key in stale_kv]
                else:
                    bch = ua_kv_cf.batch()
                    [bch.remove(key) for key in stale_kv]
                    bch.send()

        return ret_errors
    # end clean_stale_subnet_uuid

    def _remove_stale_from_uuid_table(self, dangle_prefix):
        logger = self._logger
        ret_errors = []

        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_uuid_table")
        for obj_uuid, _ in self.cassandra_get_range(obj_uuid_table, column_count=1):
            cols = dict(self.cassandra_xget(obj_uuid_table, obj_uuid))
            obj_type = json.loads(cols.get('type', '"UnknownType"'))
            fq_name = json.loads(cols.get('fq_name', '"UnknownFQN"'))
            stale_cols = []
            for col_name in cols:
                if not col_name.startswith(dangle_prefix):
                    continue
                _, _, dangle_check_uuid = col_name.split(':')
                try:
                    obj_uuid_table.get(dangle_check_uuid)
                except (KeyError, pycassa.NotFoundException):
                    msg = ("Found stale %s index: %s in %s (%s %s)" %
                           (dangle_prefix, col_name, obj_uuid, obj_type,
                            fq_name))
                    logger.info(msg)
                    stale_cols.append(col_name)

            if stale_cols:
                if not self._args.execute:
                    logger.info("Would remove stale %s: %s", dangle_prefix,
                                stale_cols)
                else:
                    logger.info("Removing stale %s: %s", dangle_prefix,
                                stale_cols)
                    self.cassandra_remove(obj_uuid_table, obj_uuid, columns=stale_cols)

        return ret_errors
    # end _remove_stale_from_uuid_table

    def _clean_zk_id_allocation(self, zk_path, cassandra_set, zk_set,
                                id_oper=None):
        logger = self._logger
        zk_path = '%s/%%s' % zk_path

        for id, fq_name_str in zk_set - cassandra_set:
            if id_oper is not None:
                id = eval(id_oper % id)
            id_str = "%(#)010d" % {'#': id}
            if not self._args.execute:
                logger.info("Would removed stale id %s for %s",
                            zk_path % id_str, fq_name_str)
            else:
                logger.info("Removing stale id %s for %s", zk_path % id_str,
                            fq_name_str)
                self.zk_delete(zk_path % id_str)

    @cleaner
    def clean_subnet_addr_alloc(self):
        """Removes extra VN's, subnets and IP's from zk."""
        logger = self._logger
        (zk_all_vns, cassandra_all_vns, duplicate_ips,
            ret_errors, stale_zk_path) = self.audit_subnet_addr_alloc()

        for path in stale_zk_path:
            if not self._args.execute:
                logger.info("Would delete zk: %s", path)
            else:
                logger.info("Deleting zk path: %s", path)
                self.zk_delete(path, recursive=True)

        # Clean extra net in zk
        extra_vn = set(zk_all_vns.keys()) - set(cassandra_all_vns.keys())
        # for a flat-network, cassandra has VN-ID as key while ZK has subnet
        # Are these extra VNs are due to flat-subnet?
        for vn in extra_vn:
            # Nothing to clean as VN in ZK has empty Subnet
            if not list(filter(bool, list(zk_all_vns[vn].values()))):
                logger.debug('Ignoring Empty VN (%s)' % vn)
                continue
            for sn_key in zk_all_vns[vn]:
                path = '%s/%s:%s' % (self.base_subnet_zk_path, vn, sn_key)
                if not self._args.execute:
                    logger.info("Would delete zk: %s", path)
                else:
                    logger.info("Deleting zk path: %s", path)
                    self.zk_delete(path, recursive=True)
            zk_all_vns.pop(vn, None)

        zk_all_vn_sn = []
        for vn_key, vn in list(zk_all_vns.items()):
            zk_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])
        cassandra_all_vn_sn = []
        # ignore subnet without address and not lock in zk
        for vn_key, vn in list(cassandra_all_vns.items()):
            for sn_key, addrs in list(vn.items()):
                if not addrs['addrs']:
                    if (vn_key not in zk_all_vns or
                            sn_key not in zk_all_vns[vn_key]):
                        continue
                cassandra_all_vn_sn.extend([(vn_key, sn_key)])

        # Clean extra subnet in zk
        extra_vn_sn = set(zk_all_vn_sn) - set(cassandra_all_vn_sn)
        # Are these extra VNs are due to flat-subnet?
        for vn, sn_key in extra_vn_sn:
            # Ignore if SN is found in Cassandra VN keys
            if any([sn_key in sdict for sdict in list(cassandra_all_vns.values())]):
                logger.debug('Ignoring SN (%s) of VN (%s) '
                             'found in cassandra' % (sn_key, vn))
                continue
            path = '%s/%s:%s' % (self.base_subnet_zk_path, vn, sn_key)
            path_no_mask = '%s/%s:%s' % (self.base_subnet_zk_path, vn,
                                         sn_key.partition('/')[0])
            if not self._args.execute:
                logger.info("Would delete zk: %s", path)
            else:
                logger.info("Deleting zk path: %s", path)
                self.zk_delete(path, recursive=True)
                if path_no_mask != path:
                    self.zk_delete(path_no_mask, recursive=False)
            if vn in zk_all_vns:
                zk_all_vns[vn].pop(sn_key, None)

        # Check for extra IP addresses in zk
        for vn, sn_key in cassandra_all_vn_sn:
            if vn not in zk_all_vns or sn_key not in zk_all_vns[vn]:
                zk_ips = []
            else:
                zk_ips = zk_all_vns[vn][sn_key]
            sn_start = cassandra_all_vns[vn][sn_key]['start']
            sn_gw_ip = cassandra_all_vns[vn][sn_key]['gw']
            sn_dns = cassandra_all_vns[vn][sn_key]['dns']
            cassandra_ips = cassandra_all_vns[vn][sn_key]['addrs']

            for ip_addr in set(zk_ips) - set(cassandra_ips):
                # ignore network, bcast and gateway ips
                if (IPAddress(ip_addr[1]) == IPNetwork(sn_key).network or
                        IPAddress(ip_addr[1]) == IPNetwork(sn_key).broadcast):
                    continue
                if (ip_addr[1] == sn_gw_ip or ip_addr[1] == sn_dns or
                        ip_addr[1] == sn_start):
                    continue

                ip_str = "%(#)010d" % {'#': int(IPAddress(ip_addr[1]))}
                path = '%s/%s:%s/%s' % (self.base_subnet_zk_path, vn, sn_key,
                                        ip_str)
                if not self._args.execute:
                    logger.info("Would delete zk: %s", path)
                else:
                    logger.info("Deleting zk path: %s", path)
                    self.zk_delete(path, recursive=True)

    @cleaner
    def clean_orphan_resources(self):
        """Removes extra VN's, subnets and IP's from zk."""
        logger = self._logger
        orphan_resources, errors = self.audit_orphan_resources()
        if not orphan_resources:
            return

        if not self._args.execute:
            logger.info("Would delete orphan resources in Cassandra DB: %s",
                        list(orphan_resources.items()))
        else:
            logger.info("Would delete orphan resources in Cassandra DB: %s",
                        orphan_resources.items())
            if self.using_json is False:
                uuid_bch = self._cf_dict['obj_uuid_table'].batch()
            else:
                obj_uuid_table = self._cf_dict['obj_uuid_table']
            for obj_type, obj_uuids in orphan_resources.items():
                if self.using_json is True:
                    [self.cassandra_remove(obj_uuid_table, obj_uuid) for obj_uuid in obj_uuids]
                else:
                    [uuid_bch.remove(obj_uuid) for obj_uuid in obj_uuids]

            if self.using_json is False:
                uuid_bch.send()
            self.clean_stale_fq_names(orphan_resources.keys())

    def _clean_if_mandatory_refs_missing(self, obj_type, mandatory_refs,
                                         ref_type='ref'):
        logger = self._logger
        fq_name_table = self._cf_dict['obj_fq_name_table']
        obj_uuid_table = self._cf_dict['obj_uuid_table']

        logger.debug("Reading %s objects from cassandra", obj_type)
        uuids = [x.split(':')[-1] for x, _ in self.cassandra_xget(fq_name_table, obj_type)]

        stale_uuids = []
        for uuid in uuids:
            missing_refs = []
            for ref in mandatory_refs:
                try:
                    cols = self.cassandra_get(
                        obj_uuid_table,
                        uuid,
                        column_start='%s:%s:' % (ref_type, ref),
                        column_finish='%s:%s;' % (ref_type, ref),
                    )
                except (pycassa.NotFoundException, KeyError):
                    missing_refs.append(True)
                    continue
                for col in cols:
                    ref_uuid = col.split(':')[-1]
                    try:
                        obj_uuid_table.get(ref_uuid)
                    except (KeyError, pycassa.NotFoundException):
                        continue
                    break
                else:
                    missing_refs.append(True)
                    continue
                missing_refs.append(False)
                break

            if all(missing_refs):
                stale_uuids.append(uuid)

        if not stale_uuids:
            return stale_uuids

        if not self._args.execute:
            logger.info("Would delete %d %s in Cassandra DB: %s",
                        len(stale_uuids), obj_type, stale_uuids)
        else:
            logger.info("Deleting %d %s in Cassandra DB: %s", len(stale_uuids),
                        obj_type, stale_uuids)
            if self.using_json is True:
                [self.cassandra_remove(obj_uuid_table, uuid) for uuid in stale_uuids]
            else:
                uuid_bch = obj_uuid_table.batch()
                [uuid_bch.remove(uuid) for uuid in stale_uuids]
                uuid_bch.send()
            self.clean_stale_fq_names([obj_type])
        return stale_uuids

    @cleaner
    def clean_stale_instance_ip(self):
        """Removes stale IIP's without virtual-network or
        virtual-machine-interface refs"""
        # Consider an IIP stale if the VN and VMI refs are in error
        self._clean_if_mandatory_refs_missing(
            'instance_ip', ['virtual_machine_interface', 'virtual_network'])

    @cleaner
    def clean_stale_route_target(self):
        """Removes stale RT's without routing-instance or
        logical-router backrefs"""
        # Consider a RT stale if does not have a valid RI or LR backref(s)
        self._clean_if_mandatory_refs_missing(
            'route_target',
            ['routing_instance', 'logical_router'],
            ref_type='backref',
        )

    @cleaner
    def clean_route_targets_routing_instance_backrefs(self):
        uuid_table = self._cf_dict['obj_uuid_table']

        errors, back_refs_to_remove =\
            self.audit_route_targets_routing_instance_backrefs()
        for (rt_fq_name_str, rt_uuid), ri_uuids in list(back_refs_to_remove.items()):
            if not self._args.execute:
                self._logger.info(
                    "Would remove RI back-refs %s from RT %s(%s)",
                    ', '.join(ri_uuids), rt_fq_name_str, rt_uuid)
            else:
                self._logger.info(
                    "Removing RI back-refs %s from RT %s(%s)",
                    ','.join(ri_uuids), rt_fq_name_str, rt_uuid)
                if self.using_json is False:
                    bch = uuid_table.batch()
                for ri_uuid in ri_uuids:
                    if self.using_json is False:
                        bch.remove(ri_uuid, 
                                columns=['ref:route_target:%s' % rt_uuid])
                        bch.remove(
                            rt_uuid,
                            columns=['backref:routing_instance:%s' % ri_uuid])
                    else:
                        self.cassandra_remove(uuid_table, ri_uuid, columns=['ref:route_target:%s' % rt_uuid])
                        self.cassandra_remove(uuid_table, rt_uuid, columns=['backref:routing_instance:%s' % ri_uuid])
                if self.using_json is False:
                    bch.send()
        return errors

class DatabaseHealer(DatabaseManager):
    def healer(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            self = args[0]
            try:
                errors = func(*args, **kwargs)
                if not errors:
                    self._logger.info('(v%s) Healer %s: Success', __version__,
                                      func.__name__)
                else:
                    self._logger.error(
                        '(v%s) Healer %s: Failed:\n%s\n',
                        __version__,
                        func.__name__,
                        '\n'.join(e.msg for e in errors),
                    )
                return errors
            except Exception as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.exception('(v%s) Healer %s: Exception, %s',
                                       __version__, func.__name__, err_msg)
                raise
        # end wrapper

        wrapper.__dict__['is_healer'] = True
        return wrapper
    # end healer

    @healer
    def heal_fq_name_index(self):
        """Creates missing rows/columns in obj_fq_name_table of cassandra for
        all entries found in obj_uuid_table if fq_name not used and only one
        founded object used that fq_name."""
        logger = self._logger
        errors = []

        fq_name_table = self._cf_dict['obj_fq_name_table']
        uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_uuid_table")
        # dict of set, key is row-key val is set of col-names
        fixups = {}
        for uuid, cols in self.cassandra_get_range(uuid_table,
                columns=['type', 'fq_name', 'prop:id_perms']):
            type = json.loads(cols.get('type', 'null'))
            fq_name = json.loads(cols.get('fq_name', 'null'))
            created_at = json.loads(cols['prop:id_perms']).get(
                'created', '"unknown"')
            if not type:
                logger.info("Unknown 'type' for object %s", uuid)
                continue
            if not fq_name:
                logger.info("Unknown 'fq_name' for object %s", uuid)
                continue
            fq_name_str = ':'.join(fq_name)
            try:
                self.cassandra_get(fq_name_table, type,
                                  columns=['%s:%s' % (fq_name_str, uuid)])
            except (KeyError, pycassa.NotFoundException):
                fixups.setdefault(type, {}).setdefault(
                    fq_name_str, set([])).add((uuid, created_at))
        # for all objects in uuid table

        for type, fq_name_uuids in list(fixups.items()):
            for fq_name_str, uuids in list(fq_name_uuids.items()):
                try:
                    fq_name_uuid_str = self.cassandra_get(
                        fq_name_table,
                        type,
                        column_start='%s:' % fq_name_str,
                        column_finish='%s;' % fq_name_str,
                    )
                except (KeyError, pycassa.NotFoundException):
                    if len(uuids) != 1:
                        msg = ("%s FQ name '%s' is used by %d resources and "
                               "not indexed: %s. Script cannot decide which "
                               "one should be heal." % (
                                   type.replace('_', ' ').title(),
                                   fq_name_str,
                                   len(uuids),
                                   ', '.join(['%s (created at %s)' % (u, c)
                                              for u, c in uuids]),
                               ))
                        errors.append(FqNameDuplicateError(msg))
                        continue
                    fq_name_uuid_str = '%s:%s' % (fq_name_str, uuids.pop()[0])
                    if not self._args.execute:
                        logger.info("Would insert FQ name index: %s %s",
                                    type, fq_name_uuid_str)
                    else:
                        logger.info("Inserting FQ name index: %s %s",
                                    type, fq_name_uuid_str)
                        cols = {fq_name_uuid_str: json.dumps(None)}
                        self.cassandra_insert(fq_name_table, type, cols)
                    continue
                # FQ name already there, check if it's a stale entry
                uuid = fq_name_uuid_str.popitem()[0].rpartition(':')[-1]
                try:
                    self.cassandra_get(uuid_table, uuid, columns=['type'])
                    # FQ name already use by an object, remove stale object
                    msg = ("%s FQ name entry '%s' already used by %s, please "
                           "run 'clean_stale_object' to remove stale "
                           "object(s): %s" % (
                               type.replace('_', ' ').title(),
                               fq_name_str, uuid,
                               ', '.join(['%s (created at %s)' % (u, c)
                                          for u, c in uuids]),
                           ))
                    logger.warning(msg)
                except (pycassa.NotFoundException, KeyError):
                    msg = ("%s stale FQ name entry '%s', please run "
                           "'clean_stale_fq_names' before trying to heal them"
                           % (type.replace('_', ' ').title(), fq_name_str))
                    logger.warning(msg)

        return errors
    # end heal_fq_name_index

    def heal_back_ref_index(self):
        return []
    # end heal_back_ref_index

    @healer
    def heal_children_index(self):
        """Creates missing children index for parents in obj_fq_name_table
        of cassandra."""
        logger = self._logger
        ret_errors = []

        obj_uuid_table = self._cf_dict['obj_uuid_table']
        logger.debug("Reading all objects from obj_uuid_table")
        # dict of set, key is parent row-key val is set of col-names
        fixups = {}
        for obj_uuid, cols in self.cassandra_get_range(obj_uuid_table, column_count=1):
            cols = dict(self.cassandra_xget(obj_uuid_table, obj_uuid, column_start='parent:',
                                            column_finish='parent;'))
            if not cols:
                continue  # no parent
            if len(cols) > 1:
                logger.info('Multiple parents %s for %s', cols, obj_uuid)
                continue

            parent_uuid = list(cols.keys())[0].split(':')[-1]
            try:
                _ = obj_uuid_table.get(parent_uuid)
            except (pycassa.NotFoundException, KeyError):
                msg = "Missing parent %s for object %s" \
                    % (parent_uuid, obj_uuid)
                logger.info(msg)
                continue

            try:
                cols = self.cassandra_get(obj_uuid_table, obj_uuid, columns=['type'])
            except (KeyError, pycassa.NotFoundException):
                logger.info("Missing type for object %s", obj_uuid)
                continue
            obj_type = json.loads(cols['type'])

            child_col = 'children:%s:%s' % (obj_type, obj_uuid)
            try:
                _ = self.cassandra_get(obj_uuid_table, parent_uuid, columns=[child_col])
                # found it, this object is indexed by parent fine
                continue
            except (KeyError, pycassa.NotFoundException):
                msg = "Found missing children index %s for parent %s" \
                    % (child_col, parent_uuid)
                logger.info(msg)

            fixups.setdefault(parent_uuid, []).append(child_col)
        # for all objects in uuid table

        for parent_uuid in fixups:
            cols = list(fixups[parent_uuid])
            if not self._args.execute:
                logger.info("Would insert row/columns: %s %s",
                            parent_uuid, cols)
            else:
                logger.info("Inserting row/columns: %s %s",
                            parent_uuid, cols)
                self.cassandra_insert(obj_uuid_table,
                        parent_uuid,
                        columns=dict((x, json.dumps(None)) for x in cols))

        return ret_errors
    # end heal_children_index

    @healer
    def heal_subnet_uuid(self):
        """Creates missing subnet uuid in useragent_keyval_table
        of cassandra."""
        logger = self._logger
        ret_errors = []

        ua_subnet_info, vnc_subnet_info, errors = self.audit_subnet_uuid()
        ret_errors.extend(errors)

        missing_ua_subnets = set(vnc_subnet_info.keys()) - \
            set(ua_subnet_info.keys())
        ua_kv_cf = self._cf_dict['useragent_keyval_table']
        for subnet_uuid in missing_ua_subnets:
            subnet_key = vnc_subnet_info[subnet_uuid]
            if not self._args.execute:
                logger.info("Would create stale subnet uuid %s -> %s in "
                            "useragent keyspace", subnet_uuid, subnet_key)
            else:
                logger.info("Creating stale subnet uuid %s -> %s in "
                            "useragent keyspace", subnet_uuid, subnet_key)
                self.cassandra_insert(ua_kv_cf, subnet_uuid, {'value': subnet_key})

        return ret_errors
    # end heal_subnet_uuid

    @healer
    def heal_virtual_networks_id(self):
        """Creates missing virtual-network id's in zk."""
        ret_errors = []

        zk_set, cassandra_set, errors, _, missing_ids =\
            self.audit_virtual_networks_id()
        ret_errors.extend(errors)

        self._heal_zk_id_allocation(self.base_vn_id_zk_path,
                                    cassandra_set,
                                    zk_set,
                                    id_oper='%%s - %d' % VN_ID_MIN_ALLOC)

        if self.using_json is False:
            zk_client = ZookeeperClient(__name__, self._api_args.zk_server_ip,
                                          self._api_args.listen_ip_addr)

        # Initialize the virtual network ID allocator
        if missing_ids and not self._args.execute:
            self._logger.info("Would allocate VN ID to %s", missing_ids)
        elif missing_ids and self._args.execute:
            obj_uuid_table = self._cf_dict['obj_uuid_table']

            if self.using_json is False:
                zk_client = ZookeeperClient(__name__, self._api_args.zk_server_ip,
                                            self._api_args.listen_ip_addr)
                id_allocator = IndexAllocator(
                    zk_client, '%s/' % self.base_vn_id_zk_path, 1 << 24)
            else:
                id_allocator = IndexJSONAllocator(self, '%s/' % self.base_vn_id_zk_path, 1 << 24)
            if self.using_json is False:
                bch = obj_uuid_table.batch()
            for uuid, fq_name_str in missing_ids:
                vn_id = id_allocator.alloc(fq_name_str) + VN_ID_MIN_ALLOC
                self._logger.info("Allocating VN ID '%d' to %s (%s)",
                                  vn_id, fq_name_str, uuid)
                cols = {'prop:virtual_network_network_id': json.dumps(vn_id)}
                if self.using_json is False:
                    bch.insert(uuid, cols)
                else:
                    self.cassandra_insert(obj_uuid_table, uuid, cols)
            if self.using_json is False:
                bch.send()

        return ret_errors
    # end heal_virtual_networks_id

    @healer
    def heal_security_groups_id(self):
        """Creates missing security-group id's in zk."""
        ret_errors = []

        zk_set, cassandra_set, errors, _, missing_ids =\
            self.audit_security_groups_id()
        ret_errors.extend(errors)

        self._heal_zk_id_allocation(self.base_sg_id_zk_path,
                                    cassandra_set,
                                    zk_set)

        # Initialize the security group ID allocator
        if missing_ids and not self._args.execute:
            self._logger.info("Would allocate SG ID to %s", missing_ids)
        elif missing_ids and self._args.execute:
            obj_uuid_table = self._cf_dict['obj_uuid_table']
            if self.using_json is False:
                zk_client = ZookeeperClient(__name__, self._api_args.zk_server_ip,
                                            self._api_args.listen_ip_addr)
                id_allocator = IndexAllocator(zk_client,
                                            '%s/' % self.base_sg_id_zk_path,
                                            1 << 32)
            else:
                id_allocator = IndexJSONAllocator(zk_client,
                        '%s/' % self.base_sg_id_zk_path,
                        1 << 32)
            if self.using_json is False:
                bch = obj_uuid_table.batch()
            for uuid, fq_name_str in missing_ids:
                sg_id = id_allocator.alloc(fq_name_str) + SG_ID_MIN_ALLOC
                self._logger.info("Allocating SG ID '%d' to %s (%s)",
                                  sg_id, fq_name_str, uuid)
                cols = {'prop:security_group_id': json.dumps(sg_id)}
                if self.using_json is False:
                    bch.insert(uuid, cols)
                else:
                    self.cassandra_insert(obj_uuid_table, uuid, cols)
            if self.using_json is False:
                bch.send()

        return ret_errors
    # end heal_security_groups_id

    def _heal_zk_id_allocation(self, zk_path, cassandra_set, zk_set,
                               id_oper=None):
        logger = self._logger
        zk_path = '%s/%%s' % zk_path

        # Add missing IDs in zk
        for id, fq_name_str in cassandra_set - zk_set:
            if id_oper is not None:
                id = eval(id_oper % id)
            id_str = "%(#)010d" % {'#': id}
            if not self._args.execute:
                try:
                    zk_fq_name_str = self.zk_get(zk_path % id_str)[0]
                    if fq_name_str != zk_fq_name_str:
                        logger.info("Would update id %s from %s to %s",
                                    zk_path % id_str, zk_fq_name_str,
                                    fq_name_str)
                except (KeyError, kazoo.exceptions.NoNodeError):
                    logger.info("Would add missing id %s for %s",
                                zk_path % id_str, fq_name_str)
            else:
                try:
                    zk_fq_name_str = self.zk_get(zk_path % id_str)[0]
                    if fq_name_str != zk_fq_name_str:
                        logger.info("Updating id %s from %s to %s",
                                    zk_path % id_str, zk_fq_name_str,
                                    fq_name_str)
                        self.zk_delete(zk_path % id_str)
                        self.zk_set(zk_path % id_str, str(fq_name_str))
                except (KeyError, kazoo.exceptions.NoNodeError):
                    logger.info("Adding missing id %s for %s",
                                zk_path % id_str, fq_name_str)
                    self.zk_create(zk_path % id_str, str(fq_name_str))

    @healer
    def heal_subnet_addr_alloc(self):
        """ Creates missing virtaul-networks, sunbets and instance-ips
        in zk."""
        logger = self._logger
        zk_all_vns, cassandra_all_vns, _, ret_errors, _ =\
            self.audit_subnet_addr_alloc()
        zk_all_vn_sn = []
        for vn_key, vn in list(zk_all_vns.items()):
            zk_all_vn_sn.extend([(vn_key, sn_key) for sn_key in vn])
        cassandra_all_vn_sn = []
        # ignore subnet without address and not lock in zk
        for vn_key, vn in list(cassandra_all_vns.items()):
            for sn_key, addrs in list(vn.items()):
                if not addrs['addrs']:
                    if (vn_key not in zk_all_vns or
                            sn_key not in zk_all_vns[vn_key]):
                        continue
                cassandra_all_vn_sn.extend([(vn_key, sn_key)])

        # Re-create missing vn/subnet in zk
        for vn, sn_key in set(cassandra_all_vn_sn) - set(zk_all_vn_sn):
            for ip_addr in cassandra_all_vns[vn][sn_key]['addrs']:
                ip_str = "%(#)010d" % {'#': int(IPAddress(ip_addr[1]))}
                path = '%s/%s:%s/%s' % (self.base_subnet_zk_path, vn, sn_key,
                                        ip_str)
                if not self._args.execute:
                    logger.info("Would create zk: %s", path)
                else:
                    logger.info("Creating zk path: %s", path)
                    self.zk_create(path, ip_addr[0], makepath=True)

        # Re-create missing IP addresses in zk
        for vn, sn_key in cassandra_all_vn_sn:
            if vn not in zk_all_vns or sn_key not in zk_all_vns[vn]:
                zk_ips = []
            else:
                zk_ips = zk_all_vns[vn][sn_key]
            cassandra_ips = cassandra_all_vns[vn][sn_key]['addrs']

            for ip_addr in set(cassandra_ips) - set(zk_ips):
                ip_str = "%(#)010d" % {'#': int(IPAddress(ip_addr[1]))}
                path = '%s/%s:%s/%s' % (self.base_subnet_zk_path, vn, sn_key,
                                        ip_str)
                if not self._args.execute:
                    logger.info("Would create zk: %s %s", path, ip_addr)
                else:
                    logger.info("Creating zk path: %s %s", path, ip_addr)
                    try:
                        self.zk_create(path, ip_addr[0], makepath=True)
                    except (kazoo.exceptions.NodeExistsError, KeyError) as e:
                        iip_uuid = self.zk_get(path)[0]
                        logger.warn("Creating zk path: %s (%s). "
                                    "Addr lock already exists for IIP %s",
                                    path, ip_addr, iip_uuid)
                        pass

        return ret_errors
# end class DatabaseCleaner

# function that is called when json is healed or cleaned
def write_json_to_out(db_man_obj):
    with open(db_man_obj.out_json, 'w') as outfile:
        json.dumps(db_man_obj.data, outfile, indent=4, sort_keys=True)

# helper function to backup json before healing and cleaning operations
def backup_json(db_man_obj):
    logger = db_man_obj._logger
    hostname = socket.gethostname()
    IPAddr = socket.gethostbyname(hostname).replace('.', '')
    logger.info("Your Computer Name is:" + hostname)
    logger.info("Your Computer IP Address is:" + IPAddr)

    # check if the backup directory exists
    if not os.path.isdir("/var/tmp/json_backups/"):
        logger.info("Creating directory")
        os.mkdir("/var/tmp/json_backups/")
    default_dir = "/var/tmp/json_backups/"

    import calendar;
    import time;
    ts = calendar.timegm(time.gmtime())
    logger.info("Saving backup at time {0}".format(ts))
    backup_file = '{0}-{1}.json'.format(IPAddr, ts)
    with open('{0}{1}'.format(default_dir, backup_file), 'w') as backup:
        db_exim = DatabaseJSONExim('--export-to {0}{1} pretty_print'.format(default_dir, backup_file))
        db_exim.db_export()

def db_check(args, api_args):
    """Checks and displays all the inconsistencies in DB."""
    vnc_cgitb.enable(format='text')

    db_checker = DatabaseChecker(args, api_args)
    # Mode and node count check across all nodes

    # no need to do in the json case as this is checking Zookeeper/PyCassa Object specific logic
    if db_checker.using_json is False:
        db_checker.check_zk_mode_and_node_count()
        db_checker.check_cassandra_keyspace_replication()
    # Obj UUID cassandra inconsistencies
    db_checker.check_obj_mandatory_fields()
    db_checker.check_orphan_resources()
    db_checker.check_fq_name_uuid_match()
    db_checker.check_duplicate_fq_name()
    # Resource link inconsistencies
    db_checker.check_route_targets_routing_instance_backrefs()
    # ID allocation inconsistencies
    db_checker.check_subnet_uuid()
    db_checker.check_subnet_addr_alloc()
    db_checker.check_route_targets_id()
    db_checker.check_virtual_networks_id()
    db_checker.check_security_groups_id()
    # db_checker.check_schema_db_mismatch()
# end db_check

db_check.is_checker = True

def db_clean(args, api_args):
    """Removes stale entries from DB's."""
    vnc_cgitb.enable(format='text')
    db_cleaner = DatabaseCleaner(args, api_args)
    if db_cleaner.backup:
        backup_json(db_cleaner)

    # Obj UUID cassandra inconsistencies
    db_cleaner.clean_obj_missing_mandatory_fields()
    db_cleaner.clean_orphan_resources()
    db_cleaner.clean_stale_fq_names()
    db_cleaner.clean_stale_object()
    db_cleaner.clean_vm_with_no_vmi()
    db_cleaner.clean_stale_route_target()
    db_cleaner.clean_route_targets_routing_instance_backrefs()
    db_cleaner.clean_stale_instance_ip()
    db_cleaner.clean_stale_back_refs()
    db_cleaner.clean_stale_refs()
    db_cleaner.clean_stale_children()
    # ID allocation inconsistencies
    db_cleaner.clean_stale_subnet_uuid()
    db_cleaner.clean_stale_route_target_id()
    db_cleaner.clean_stale_virtual_network_id()
    db_cleaner.clean_stale_security_group_id()
    db_cleaner.clean_subnet_addr_alloc()

    if db_cleaner._args.execute is False:
        if db_cleaner.out_json is not None:
            db_cleaner._logger.error("Need to specify execute args to Clean JSON file")
    else:
        if db_cleaner.out_json is not None:
            write_json_to_out(db_cleaner)
        else:
            db_cleaner._logger.error("Need to specify output file if going to execute Clean operations")

# end db_clean
db_clean.is_cleaner = True

def db_heal(args, api_args):
    """Creates missing entries in DB's for all inconsistencies."""
    vnc_cgitb.enable(format='text')
    db_healer = DatabaseHealer(args, api_args)

    # if backup option is enabled then store backup
    if db_healer.backup:
        backup_json(db_healer)
    # Obj UUID cassandra inconsistencies
    db_healer.heal_fq_name_index()
    db_healer.heal_back_ref_index()
    db_healer.heal_children_index()
    # ID allocation inconsistencies
    db_healer.heal_subnet_uuid()
    db_healer.heal_virtual_networks_id()
    db_healer.heal_security_groups_id()
    db_healer.heal_subnet_addr_alloc()
    if db_healer._args.execute is False:
        if db_healer.out_json is not None:
            db_healer._logger.error("Need to specify execute arg to Heal JSON file")
    else:
        if db_healer.out_json is not None:
            write_json_to_out(db_healer)
        else:
            db_healer._logger.error("Need to specify output file if going to execute Heal operations")
# end db_heal
db_heal.is_healer = True

def db_touch_latest(args, api_args):
    vnc_cgitb.enable(format='text')

    db_mgr = DatabaseManager(args, api_args)
    obj_uuid_table = db_mgr._cf_dict['obj_uuid_table']

    for obj_uuid, cols in db_mgr.cassandra_get_range(obj_uuid_table, column_count=1):
        db_mgr.cassandra_insert(
                db_mgr._cf_dict['obj_uuid_table'],
                obj_uuid,
                columns={'META:latest_col_ts': json.dumps(None)})

# end db_touch_latest
db_touch_latest.is_operation = True

def main():
    args, api_args = _parse_args(' '.join(sys.argv[1:]))
    verb = args.operation
    if 'db_%s' % (verb) in globals():
        return globals()['db_%s' % (verb)](args, api_args)

    if getattr(DatabaseChecker, verb, None):
        db_checker = DatabaseChecker(args, api_args)
        return getattr(db_checker, verb)()

    if getattr(DatabaseCleaner, verb, None):
        db_cleaner = DatabaseCleaner(args, api_args)
        return getattr(db_cleaner, verb)()

    if getattr(DatabaseHealer, verb, None):
        db_healer = DatabaseHealer(args, api_args)
        return getattr(db_healer, verb)()

    print("Warning: Unknown operation '%s'\n\t Use --help" % verb)
# end main

if __name__ == '__main__':
    main()
