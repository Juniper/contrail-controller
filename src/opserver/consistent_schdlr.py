#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from consistent_hash import ConsistentHash
import gevent
import hashlib
import logging
from kazoo.client import KazooClient
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
    def __init__(self, service_name=None, zookeeper='127.0.0.1:2181',
                 delete_hndlr=None, add_hndlr=None, bucketsize=47,
                 item2part_func=None, partitioner=None, logger=None):
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
        self._zk_path = '/'.join(['/contrail_cs', self._service_name])
        self._zk = KazooClient(self._zookeeper_srvr)
        self._zk.start()
        self._pc = self._zk.SetPartitioner(path=self._zk_path,
                                           set=self._partition_set,
                                           partition_func=self._partitioner)
        gevent.sleep(0)

    def schedule(self, items, lock_timeout=30):
        gevent.sleep(0)
        ret = False
        if self._pc.failed:
            raise Exception("Lost or unable to acquire partition")
        elif self._pc.release:
            self._supress_log('Releasing...')
            self._release()
        elif self._pc.allocating:
            self._supress_log('Waiting for allocation...')
            self._pc.wait_for_acquire(lock_timeout)
        elif self._pc.acquired:
            self._supress_log('got work: ', list(self._pc))
            ret = True
            self._populate_work_items(items)
            self._supress_log('work items: ',
                              self._items2name(self.work_items()),
                              'from the list',
                              self._items2name(items))
        return ret

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
            self._supress_log('members:', self._con_hash.nodes)
        cur, updtd = set(self._con_hash.nodes), set(members)
        if cur != updtd:
            newm = updtd - cur
            rmvd = cur - updtd
            if newm:
                self._supress_log('new workers:', newm)
                self._con_hash.add_nodes(list(newm))
            if rmvd:
                self._supress_log('workers left:', rmvd)
                self._con_hash.del_nodes(list(rmvd))
        return self._con_hash

    def _consistent_hash_get_node(self, members, partition):
        return self._consistent_hash(members).get_node(partition)

    def _partitioner_func(self, identifier, members, _partitions):
        return [p for p in _partitions \
                if self._consistent_hash_get_node(members, p) == identifier]

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
