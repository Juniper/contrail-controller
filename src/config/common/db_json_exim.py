# Usage: python db_json_exim.py --import-from /import-data/db.json
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
from pycassa.system_manager import SystemManager
import kazoo.client
import kazoo.handlers.gevent

from cfgm_common.vnc_cassandra import VncCassandraClient
from vnc_cfg_api_server import utils

logger = logging.getLogger(__name__)

class CassandraNotEmptyError(Exception): pass
class ZookeeperNotEmptyError(Exception): pass
class InvalidArguments(Exception): pass

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
        self._cassandra = VncCassandraClient(
            self._api_args.cassandra_server_list, self._api_args.cluster_id,
            rw_keyspaces=ks_cf_info, ro_keyspaces=None, logger=self.log,
            reset_config=False)
    # end init_cassandra

    def log(self, msg, level):
        pass
    # end log

    def _parse_args(self, args_str):
        parser = argparse.ArgumentParser()
  
        help="Path to contrail-api conf file, default /etc/contrail-api.conf"
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

        # check older format export file which had only config_db_uuid
        # CF names at top-level
        if set(['obj_uuid_table', 'obj_fq_name_table']) == set(
                self.import_data['cassandra'].keys()):
            self.init_cassandra()
        else:
            try:
                # in pre 3.1 releases, tuple for cf_info not dict
                ks_cf_info = dict((ks, [(c, None) for c in cf.keys()]) 
                    for ks,cf in self.import_data['cassandra'].items())
                self.init_cassandra(ks_cf_info)
            except TypeError as e:
                if not 'list indices must be integers, not tuple' in e:
                    raise
                ks_cf_info = dict((ks, dict((c, {}) for c in cf.keys()))
                    for ks,cf in self.import_data['cassandra'].items())
                self.init_cassandra(ks_cf_info)

        # refuse import if db already has data
        if len(list(self._cassandra.get_cf('obj_uuid_table').get_range(column_count=0))) > 0:
            raise CassandraNotEmptyError('obj_uuid_table has entries')
        if len(list(self._cassandra.get_cf('obj_fq_name_table').get_range(column_count=0))) > 0:
            raise CassandraNotEmptyError('obj_fq_name_table has entries')
        zk_nodes = self._zookeeper.get_children('/')

        zk_ignore_list = ['consumers', 'config', 'controller', 
            'isr_change_notification', 'admin', 'brokers', 'zookeeper',
            'controller_epoch']
        for ignore in zk_ignore_list:
            try:
                zk_nodes.remove(ignore)
            except ValueError:
                pass
        if len(zk_nodes) > 0:
            raise ZookeeperNotEmptyError('Zookeeper has entries')

        # seed cassandra
        if 'obj_uuid_table' in self.import_data['cassandra']:
            # old format only fqn and uuid table were exported at top-level
            for cf_name in ['obj_fq_name_table', 'obj_uuid_table']:
                cf = self._cassandra.get_cf(cf_name)
                for row,cols in self.import_data['cassandra'][cf_name].items():
                    for col_name, col_val_ts in cols.items():
                        cf.insert(row, {col_name: col_val_ts[0]})
        else:
            for ks_name in self.import_data['cassandra'].keys():
                for cf_name in self.import_data['cassandra'][ks_name].keys():
                    cf = self._cassandra.get_cf(cf_name)
                    for row,cols in self.import_data['cassandra'][ks_name][cf_name].items():
                        for col_name, col_val_ts in cols.items():
                            cf.insert(row, {col_name: col_val_ts[0]})
        # end seed cassandra

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

    def db_export(self):
        db_contents = {'cassandra': {},
                       'zookeeper': {}}

        cassandra_contents = db_contents['cassandra']
        for ks_name in ['config_db_uuid',
            'useragent',
            'to_bgp_keyspace',
            'svc_monitor_keyspace',
            'DISCOVERY_SERVER',]:
            cassandra_contents[ks_name] = {}
            if ks_name == 'DISCOVERY_SERVER':
                # stringify key as composite column is used
                stringify_col_name = True
            else:
                stringify_col_name = False

            pool = pycassa.ConnectionPool(
                ks_name, [self._api_args.cassandra_server_list],
                pool_timeout=120, max_retries=-1, timeout=5)
            sys_mgr = SystemManager(self._api_args.cassandra_server_list[0],
                credentials={'username': self._api_args.cassandra_user,
                             'password': self._api_args.cassandra_password})
            for cf_name in sys_mgr.get_keyspace_column_families(ks_name):
                cassandra_contents[ks_name][cf_name] = {}
                cf = pycassa.ColumnFamily(pool, cf_name)
                for r,c in cf.get_range(column_count=10000000, include_timestamp=True):
                    if stringify_col_name:
                        cassandra_contents[ks_name][cf_name][r] = dict(
                            (str(k), v) for k,v in c.items())
                    else:
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
        nodes = get_nodes('/')
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
