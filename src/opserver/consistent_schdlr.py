#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from consistent_hash import ConsistentHash
import gevent
import os
import hashlib
import logging
from kazoo.client import KazooClient
from kazoo.client import KazooState
from kazoo.handlers.gevent import SequentialGeventHandler
from random import randint
import struct
import traceback

class ConsistentScheduler(object):
    '''
        LibPartitionHelper abstract out workers and work_items, and their
        mapping to partitions. So application can only deal with the work
        items it owns, without bothering about partition mapping.

        This class also provides syncronization premitives to ensure apps
        to clean up b4 giving up their partitions
    '''
    _MAX_WAIT_4_ALLOCATION = 6 + randint(0, 9)

    def __init__(self, service_name=None, zookeeper='127.0.0.1:2181',
                 delete_hndlr=None, add_hndlr=None, bucketsize=47,
                 item2part_func=None, partitioner=None, logger=None, 
                 cluster_id=''):
        if logger:
            self._logger = logger
        else:
            self._logger = logging.getLogger(__name__)
        self._service_name = service_name or os.path.basename(sys.argv[0])
        self._item2part_func = item2part_func or self._device2partition
        self._zookeeper_srvr = zookeeper
        self._bucketsize = bucketsize
        self._delete_hndlr = delete_hndlr
        self._add_hndlr = add_hndlr
        self._partitioner = partitioner or self._partitioner_func
        self._partitions = {}
        self._con_hash = None
        self._last_log = ''
        self._last_log_cnt = 0
        self._partition_set = map(str, range(self._bucketsize))

        self._cluster_id = cluster_id
        if self._cluster_id:
            self._zk_path = '/'+self._cluster_id + '/contrail_cs' + '/'+self._service_name
        else:
            self._zk_path = '/'.join(['/contrail_cs', self._service_name])
        self._zk = KazooClient(self._zookeeper_srvr,
            handler=SequentialGeventHandler())
        self._zk.add_listener(self._zk_lstnr)
        self._conn_state = None
        while True:
            try:
                self._zk.start()
                break
            except gevent.event.Timeout as e:
                # Update connection info
                self._sandesh_connection_info_update(status='DOWN',
                                                     message=str(e))
                gevent.sleep(1)
            # Zookeeper is also throwing exception due to delay in master election
            except Exception as e:
                # Update connection info
                self._sandesh_connection_info_update(status='DOWN',
                                                     message=str(e))
                gevent.sleep(1)
        self._pc = self._zk.SetPartitioner(path=self._zk_path,
                                           set=self._partition_set,
                                           partition_func=self._partitioner)
        self._wait_allocation = 0
        gevent.sleep(0)

    def _sandesh_connection_info_update(self, status, message):
        from pysandesh.connection_info import ConnectionState
        from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
            ConnectionType

        new_conn_state = getattr(ConnectionStatus, status)
        ConnectionState.update(conn_type = ConnectionType.ZOOKEEPER,
                name = 'Zookeeper', status = new_conn_state,
                message = message,
                server_addrs = self._zookeeper_srvr.split(','))

        if ((self._conn_state and self._conn_state != ConnectionStatus.DOWN) and
            new_conn_state == ConnectionStatus.DOWN):
            msg = 'Connection to Zookeeper down: %s' %(message)
            self._supress_log(msg)
        if (self._conn_state and self._conn_state != new_conn_state and
            new_conn_state == ConnectionStatus.UP):
            msg = 'Connection to Zookeeper ESTABLISHED'
            self._supress_log(msg)

        self._conn_state = new_conn_state
    # end _sandesh_connection_info_update

    def _zk_lstnr(self, state):
        if state == KazooState.CONNECTED:
            # Update connection info
            self._sandesh_connection_info_update(status='UP', message='')
        elif state == KazooState.LOST:
            # Lost the session with ZooKeeper Server
            # Best of option we have is to exit the process and restart all 
            # over again
            self._sandesh_connection_info_update(status='DOWN',
                                      message='Connection to Zookeeper lost')
            os._exit(2)
        elif state == KazooState.SUSPENDED:
            # Update connection info
            self._sandesh_connection_info_update(status='INIT',
                message = 'Connection to zookeeper lost. Retrying')

    def schedule(self, items, lock_timeout=30):
        gevent.sleep(0)
        ret = False
        if self._pc.failed:
            self._logger.error('Lost or unable to acquire partition')
            os._exit(2)
        elif self._pc.release:
            self._supress_log('Releasing...')
            self._release()
        elif self._pc.allocating:
            self._supress_log('Waiting for allocation...')
            self._pc.wait_for_acquire(lock_timeout)
            if self._wait_allocation < self._MAX_WAIT_4_ALLOCATION:
                self._wait_allocation += 1
            else:
                self._logger.error('Giving up after %d tries!' %
                    (self._wait_allocation))
                os._exit(2)
        elif self._pc.acquired:
            self._supress_log('got work: ', list(self._pc))
            ret = True
            self._wait_allocation = 0
            self._populate_work_items(items)
            self._supress_log('work items: ',
                              self._items2name(self.work_items()),
                              'from the list',
                              self._items2name(items))
        return ret

    def members(self):
        return list(self._con_hash.nodes)

    def partitions(self):
        return list(self._pc)

    def work_items(self):
        return sum(self._partitions.values(), [])

    def finish(self):
        self._inform_delete(self._partitions.keys())
        self._pc.finish()

    def _items2name(self, items):
        return map(lambda x: x.name, items)

    def _supress_log(self, *s):
        slog = ' '.join(map(str, s))
        dl = ''
        if slog != self._last_log_cnt:
            if self._last_log_cnt:
                dl += ' ' * 4
                dl += '.' * 8
                dl += '[last print repeats %d times]' % self._last_log_cnt
                self._last_log_cnt = 0
            dl += slog
            self._last_log = slog
            self._logger.debug(dl)
        else:
            self._last_log_cnt += 1

    def _consistent_hash(self, members):
        if self._con_hash is None:
            self._con_hash = ConsistentHash(members)
            self._logger.error('members: %s' % (str(self._con_hash.nodes)))
        cur, updtd = set(self._con_hash.nodes), set(members)
        if cur != updtd:
            newm = updtd - cur
            rmvd = cur - updtd
            if newm:
                self._logger.error('new members: %s' % (str(newm)))
                self._con_hash.add_nodes(list(newm))
            if rmvd:
                self._logger.error('members left: %s' % (str(rmvd)))
                self._con_hash.del_nodes(list(rmvd))
        return self._con_hash

    def _consistent_hash_get_node(self, members, partition):
        return self._consistent_hash(members).get_node(partition)

    def _partitioner_func(self, identifier, members, _partitions):
        partitions = [p for p in _partitions \
            if self._consistent_hash_get_node(members, p) == identifier]
        self._logger.error('partitions: %s' % (str(partitions)))
        return partitions

    def _release(self):
        old = set(self._pc)
        new = set(self._partitioner(self._pc._identifier,
                                   list(self._pc._party),
                                   self._partition_set))
        rmvd = old - new
        added = new - old
        if rmvd:
            self._inform_delete(list(rmvd))
        if added:
            self._inform_will_add(list(added))
        self._pc.release_set()

    def _list_items_in(self, partitions):
        return sum([self._partitions[k] for k in partitions if k in \
                    self._partitions], [])

    def _inform_will_add(self, partitions):
        if callable(self._add_hndlr):
            self._add_hndlr(self._list_items_in(partitions))

    def _inform_delete(self, partitions):
        if callable(self._delete_hndlr):
            self._delete_hndlr(self._list_items_in(partitions))

    def _populate_work_items(self, items):
        self._refresh_work_items()
        for i in items:
            part = str(self._item2part_func(i.name))
            if part in list(self._pc):
                if part not in self._partitions:
                    self._partitions[part] = []
                if i.name not in map(lambda x: x.name,
                                     self._partitions[part]):
                    self._partitions[part].append(i)
        self._logger.debug('@populate_work_items(%s): done!' % ' '.join(
                map(lambda v: str(v[0]) + ':' + ','.join(map(
                        lambda x: x.name, v[1])), self._partitions.items())))
        gevent.sleep(0)

    def _device2partition(self, key):
        return struct.unpack('Q', hashlib.md5(key).digest(
                    )[-8:])[0] % self._bucketsize

    def _refresh_work_items(self):
        for k in self._partitions:
            self._partitions[k] = []
