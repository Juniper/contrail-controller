# Preusage steps:
#Stop all contrail services including zookeeper.
#Remove/rename /var/lib/zookeeper/version-2 
#Start Cassandra-database and zookeeper
#Run the provided script to load the db.
#Start all the services and wait till they are all up.

# Usage: python db-import-json.py --import-from /import-data/db.json
import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import logging
import argparse
import gzip
import json
import cgitb
import gevent

import kazoo.client
import kazoo.handlers.gevent

from cfgm_common.vnc_cassandra import VncCassandraClient
from vnc_cfg_api_server import utils

logger = logging.getLogger(__name__)

class CassandraNotEmptyError(Exception): pass
class ZookeeperNotEmptyError(Exception): pass

class DatabaseExim(object):
    def __init__(self, args_str):
        self._parse_args(args_str)
        self._cassandra = VncCassandraClient(
            self._api_args.cassandra_server_list,
            self._api_args.cluster_id, None, None, logger=self.log)


        self._zookeeper = kazoo.client.KazooClient(
            self._api_args.zk_server_ip,
            timeout=400,
            handler=kazoo.handlers.gevent.SequentialGeventHandler())
        self._zookeeper.start()
    # end __init__

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
            metavar='FILE', default='db.json')
        parser.add_argument(
            "--export-to", help="Export from database to this json file",
            metavar='FILE')

        args_obj, remaining_argv = parser.parse_known_args(args_str.split())
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

        # refuse import if db already has data
        if len(list(self._cassandra.get_cf('obj_uuid_table').get_range(column_count=0))) > 0:
            raise CassandraNotEmptyError('obj_uuid_table has entries')
        if len(list(self._cassandra.get_cf('obj_fq_name_table').get_range(column_count=0))) > 0:
            raise CassandraNotEmptyError('obj_fq_name_table has entries')
        zk_nodes = self._zookeeper.get_children('/')
        zk_nodes.remove('zookeeper')
        if len(zk_nodes) > 0:
            raise ZookeeperNotEmptyError('Zookeeper has entries')
        # seed cassandra
        for cf_name in ['obj_fq_name_table', 'obj_uuid_table']:
            cf = self._cassandra.get_cf(cf_name)
            for row, columns in self.import_data['cassandra']['config_db_uuid'][cf_name].items():
                for col_name, col_val_ts in columns.items():
                    cf.insert(row, {col_name: col_val_ts[0]})

        # seed zookeeper
        for path_value_ts in json.loads(self.import_data['zookeeper']):
            path = path_value_ts[0]
            if path.endswith('/'):
                path = path[:-1]
            if (path.startswith('/api-server') or
                path.startswith('fq-name-to-uuid') or
                path.startswith('id')):
                value = path_value_ts[1][0]
                self._zookeeper.create(path, str(value), makepath=True)
    # end db_import

    def db_export(self):
        pass
    # end db_export
# end class DatabaseExim

def main(args_str):
    cgitb.enable(format='text')
    db_exim = DatabaseExim(args_str)
    db_exim.db_import()
    pass
# end main

if __name__ == '__main__':
    main(' '.join(sys.argv[1:]))
