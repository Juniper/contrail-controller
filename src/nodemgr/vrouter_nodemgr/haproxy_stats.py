import os
import socket
import sys
import csv

HAPROXY_DIR = '/var/lib/contrail/loadbalancer/haproxy/'

STATS_MAP = {
    'active_connections': 'qcur',
    'max_connections': 'qmax',
    'current_sessions': 'scur',
    'max_sessions': 'smax',
    'total_sessions': 'stot',
    'bytes_in': 'bin',
    'bytes_out': 'bout',
    'connection_errors': 'econ',
    'response_errors': 'eresp',
    'status': 'status',
    'health': 'check_status',
    'failed_checks': 'chkfail'
}

# 1 + 2 + 4 = 7 for frontend + backend + server
REQUEST_TYPE = 7

# response types
TYPE_FRONTEND_RESPONSE = '0'
TYPE_BACKEND_RESPONSE = '1'
TYPE_SERVER_RESPONSE = '2'

class HaproxyStats(object):
    def __init__(self):
        pass

    def get_stats(self, pool_id):
        sock_path = os.path.join(LB_BASE_DIR, pool_id, 'haproxy.sock')
        if not os.path.exists(sock_path):
            sys.stderr.write('\nStats socket not found for pool ' + pool_id)
            return {}

        lb_stats = {}
        raw_stats = self._read_stats(sock_path)
        for row in csv.DictReader(raw_stats.lstrip('# ').splitlines()):
            if row.get('type') == TYPE_FRONTEND_RESPONSE:
                lb_stats['vip'] = self._get_stats(row, row['pxname'])
            elif row.get('type') == TYPE_BACKEND_RESPONSE:
                lb_stats['pool'] = self._get_stats(row, row['pxname'])
            elif row.get('type') == TYPE_SERVER_RESPONSE:
                lb_stats.setdefault('members', [])
                lb_stats['members'].append(self._get_stats(row, row['svname']))
        return lb_stats

    def _get_stats(self, row, name):
        stats = dict((k, row.get(v, ''))
                     for k, v in STATS_MAP.items())
        stats['name'] = name
        stats['vrouter'] = socket.gethostname()
        if stats['status'] in ['no check', 'UP', 'OPEN']:
            stats['status'] = 'ACTIVE'
        else:
            stats['status'] = 'DOWN'
        return stats

    def _read_stats(self, socket_path):
        raw_stats = ''
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(socket_path)
            s.send('show stat -1 %s -1\n' % REQUEST_TYPE)
            chunk_size = 1024
            while True:
                chunk = s.recv(chunk_size)
                raw_stats += chunk
                if len(chunk) < chunk_size:
                    break
        except socket.error as e:
            sys.stderr.write('\nstats socket error: ' + str(e))

        return raw_stats
