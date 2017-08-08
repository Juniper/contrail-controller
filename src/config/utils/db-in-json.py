# e.g pycassaShell -H <db-node-ip> -f db_in_json.py  | sed -e '1,/contents:/d' | python -m json.tool
from pprint import pprint
import json
import kazoo.client

db_contents = {'cassandra': {},
               'zookeeper': {}}

cassandra_contents = db_contents['cassandra']
for ks_name in ['config_db_uuid',
                'useragent',
                #'DISCOVERY_SERVER',
                'to_bgp_keyspace',
                'svc_monitor_keyspace',]:
    cassandra_contents[ks_name] = {}
    if ks_name == 'DISCOVERY_SERVER':
        # stringify key as composite column is used
        stringify_col_name = True
    else:
        stringify_col_name = False
    pool = pycassa.ConnectionPool(ks_name, [hostname], pool_timeout=120,
                max_retries=-1, timeout=5)
    for cf_name in SYSTEM_MANAGER.get_keyspace_column_families(ks_name):
        cassandra_contents[ks_name][cf_name] = {}
        cf = pycassa.ColumnFamily(pool, cf_name)
        for r,c in cf.get_range(column_count=10000000, include_timestamp=True):
            if stringify_col_name:
                cassandra_contents[ks_name][cf_name][r] = dict((str(k), v) for k,v in c.items())
            else:
                cassandra_contents[ks_name][cf_name][r] = c

def get_nodes(path):
    if not zk.get_children(path):
        return [(path, zk.get(path))]
    nodes = []
    for child in zk.get_children(path):
        nodes.extend(get_nodes('%s%s/' %(path, child)))
    return nodes

zk = kazoo.client.KazooClient(hostname)
zk.start()
nodes = get_nodes('/')
zk.stop()
db_contents['zookeeper'] = json.dumps(nodes)

print 'contents:'
print json.dumps(db_contents)
exit()
