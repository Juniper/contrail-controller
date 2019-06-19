# USAGE STEPS:
# To upload db
# Stop all contrail services including zookeeper.
# Remove/rename /var/lib/zookeeper/version-2
# Remove/rename /var/lib/cassandra/data
# Start Cassandra-database and zookeeper
# Run the provided script to load the db.
# python db_json_exim.py --import-from /import-data/db.json
# Start all the services and wait till they are all up.
# Provision control node

# To take a db snapshot
# python db_json_exim.py --export-to <filename>


import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import logging
import argparse
import gzip
import json
import cgitb
import gevent

import pycassa
import pycassa.connection
from pycassa.system_manager import SystemManager
import kazoo.client
import kazoo.handlers.gevent
from thrift.transport import TSSLSocket
import ssl

from cfgm_common.vnc_cassandra import VncCassandraClient
from vnc_cfg_api_server import utils

logger = logging.getLogger(__name__)

class CassandraNotEmptyError(Exception): pass
class ZookeeperNotEmptyError(Exception): pass
class InvalidArguments(Exception): pass

KEYSPACES = ['config_db_uuid',
            'useragent',
            'to_bgp_keyspace',
            'svc_monitor_keyspace',
            'dm_keyspace']

class DatabaseExim(object):
    def __init__(self, args_str):
        self._parse_args(args_str)

        self._zookeeper = kazoo.client.KazooClient(
            self._api_args.zk_server_ip,
            timeout=400,
            handler=kazoo.handlers.gevent.SequentialGeventHandler())
        self._zookeeper.start()
    # end __init__

    def init_cassandra(self, ks_cf_info=None):
        ssl_enabled = (self._api_args.cassandra_use_ssl
                       if 'cassandra_use_ssl' in self._api_args else False)
        self._cassandra = VncCassandraClient(
            self._api_args.cassandra_server_list, self._api_args.cluster_id,
            rw_keyspaces=ks_cf_info, ro_keyspaces=None, logger=self.log,
            reset_config=False,
            ssl_enabled=self._api_args.cassandra_use_ssl,
            ca_certs=self._api_args.cassandra_ca_certs)
    # end init_cassandra

    def log(self, msg, level):
        pass
    # end log

    def _parse_args(self, args_str):
        parser = argparse.ArgumentParser()

        help="Path to contrail-api conf file, default /etc/contrail/contrail-api.conf"
        parser.add_argument(
            "--api-conf", help=help, default="/etc/contrail/contrail-api.conf")
        parser.add_argument(
            "--verbose", help="Run in verbose/INFO mode, default False",
            action='store_true', default=False)
        parser.add_argument(
            "--debug", help="Run in debug mode, default False",
            action='store_true', default=False)
        parser.add_argument(
            "--import-from", help="Import from this json file to database",
            metavar='FILE')
        parser.add_argument(
            "--export-to", help="Export from database to this json file",
            metavar='FILE')
        parser.add_argument(
            "--omit-keyspaces",
            nargs='*',
            help="List of keyspaces to omit in export/import",
            metavar='FILE')

        args_obj, remaining_argv = parser.parse_known_args(args_str.split())
        if ((args_obj.import_from is not None) and
            (args_obj.export_to is not None)):
            raise InvalidArguments(
                'Both --import-from and --export-to cannot be specified %s' %(
                args_obj))
        self._args = args_obj

        self._api_args = utils.parse_args('-c %s %s'
            %(self._args.api_conf, ' '.join(remaining_argv)))[0]
        pass
    # end _parse_args

    def db_import(self):
        if self._args.import_from.endswith('.gz'):
            try:
                f = gzip.open(self._args.import_from, 'rb')
                self.import_data = json.loads(f.read())
            finally:
                f.close()
        else:
            with open(self._args.import_from, 'r') as f:
                self.import_data = json.loads(f.read())

        ks_cf_info = dict((ks, dict((c, {}) for c in cf.keys()))
            for ks,cf in self.import_data['cassandra'].items())
        self.init_cassandra(ks_cf_info)

        # refuse import if db already has data
        non_empty_errors = []
        for ks in self.import_data['cassandra'].keys():
            for cf in self.import_data['cassandra'][ks].keys():
                if len(list(self._cassandra.get_cf(cf).get_range(
                    column_count=0))) > 0:
                    non_empty_errors.append(
                        'Keyspace %s CF %s already has entries.' %(ks, cf))

        if non_empty_errors:
            raise CassandraNotEmptyError('\n'.join(non_empty_errors))

        non_empty_errors = []
        existing_zk_dirs = set(
            self._zookeeper.get_children(self._api_args.cluster_id+'/'))
        import_zk_dirs = set([p_v_ts[0].split('/')[1]
            for p_v_ts in json.loads(self.import_data['zookeeper'] or "[]")])

        for non_empty in ((existing_zk_dirs & import_zk_dirs) - 
                          set(['zookeeper'])):
            non_empty_errors.append(
                'Zookeeper has entries at /%s.' %(non_empty))

        if non_empty_errors:
            raise ZookeeperNotEmptyError('\n'.join(non_empty_errors))

        # seed cassandra
        for ks_name in self.import_data['cassandra'].keys():
            for cf_name in self.import_data['cassandra'][ks_name].keys():
                cf = self._cassandra.get_cf(cf_name)
                for row,cols in self.import_data['cassandra'][ks_name][cf_name].items():
                    for col_name, col_val_ts in cols.items():
                        cf.insert(row, {col_name: col_val_ts[0]})
        # end seed cassandra

        zk_ignore_list = ['consumers', 'config', 'controller',
                          'isr_change_notification', 'admin', 'brokers',
                          'zookeeper', 'controller_epoch',
                          'api-server-election', 'schema-transformer',
                          'device-manager', 'svc-monitor', 'contrail_cs',
                          'lockpath', 'analytics-discovery-',
                          'analytics-discovery-' + self._api_args.cluster_id]
        # seed zookeeper
        for path_value_ts in json.loads(self.import_data['zookeeper'] or "{}"):
            path = path_value_ts[0]
            if path.endswith('/'):
                path = path[:-1]
            if path.split('/')[1] in zk_ignore_list:
                continue
            value = path_value_ts[1][0]
            self._zookeeper.create(path, str(value), makepath=True)
    # end db_import

    def _make_ssl_socket_factory(self, ca_certs, validate=True):
        # copy method from pycassa library because no other method
        # to override ssl version
        def ssl_socket_factory(host, port):
            TSSLSocket.TSSLSocket.SSL_VERSION = ssl.PROTOCOL_TLSv1_2
            return TSSLSocket.TSSLSocket(host, port, ca_certs=ca_certs, validate=validate)
        return ssl_socket_factory

    def db_export(self):
        db_contents = {'cassandra': {},
                       'zookeeper': {}}

        cassandra_contents = db_contents['cassandra']
        for ks_name in (set(KEYSPACES) -
                        set(self._args.omit_keyspaces or [])):
            if self._api_args.cluster_id:
                full_ks_name = '%s_%s' %(self._api_args.cluster_id, ks_name)
            else:
                full_ks_name = ks_name
            cassandra_contents[ks_name] = {}

            socket_factory = pycassa.connection.default_socket_factory
            if ('cassandra_use_ssl' in self._api_args and
                self._api_args.cassandra_use_ssl):
                socket_factory = self._make_ssl_socket_factory(
                    self._api_args.cassandra_ca_certs, validate=False)

            creds = None
            if (self._api_args.cassandra_user and
                self._api_args.cassandra_password):
                creds = {'username': self._api_args.cassandra_user,
                         'password': self._api_args.cassandra_password}
            pool = pycassa.ConnectionPool(
                full_ks_name, self._api_args.cassandra_server_list,
                pool_timeout=120, max_retries=-1, timeout=5,
                socket_factory=socket_factory, credentials=creds)
            sys_mgr = SystemManager(self._api_args.cassandra_server_list[0],
                credentials=creds, socket_factory=socket_factory)
            for cf_name in sys_mgr.get_keyspace_column_families(full_ks_name):
                cassandra_contents[ks_name][cf_name] = {}
                cf = pycassa.ColumnFamily(pool, cf_name)
                for r,c in cf.get_range(column_count=10000000, include_timestamp=True):
                    cassandra_contents[ks_name][cf_name][r] = c

        def get_nodes(path):
            if not zk.get_children(path):
                return [(path, zk.get(path))]

            nodes = []
            for child in zk.get_children(path):
                nodes.extend(get_nodes('%s%s/' %(path, child)))

            return nodes

        zk = kazoo.client.KazooClient(self._api_args.zk_server_ip)
        zk.start()
        nodes = get_nodes(self._api_args.cluster_id+'/')
        zk.stop()
        db_contents['zookeeper'] = json.dumps(nodes)

        f = open(self._args.export_to, 'w')
        try:
            f.write(json.dumps(db_contents))
        finally:
            f.close()
    # end db_export
# end class DatabaseExim

def main(args_str):
    cgitb.enable(format='text')
    try:
        db_exim = DatabaseExim(args_str)
    except InvalidArguments as e:
        print str(e)
        return
    if 'import-from' in args_str:
        db_exim.db_import()
    if 'export-to' in args_str:
        db_exim.db_export()
# end main

if __name__ == '__main__':
    main(' '.join(sys.argv[1:]))
