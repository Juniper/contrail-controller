import gevent
from gevent.coros import Semaphore
import hashlib
from libpartition import PartitionClient
import struct
import traceback


class LibPartitionHelper(object):
    '''
        LibPartitionHelper abstract out workers and work_items, and their
        mapping to partitions. So application can only deal with the work
        items it owns, without bothering about partition mapping.

        This class also provides syncronization premitives to ensure apps
        to clean up b4 giving up their partitions
    '''
    def __init__(self, name, service_name=None, disc_client=None,
                 zookeeper='127.0.0.1:2181', delete_hndlr=None,
                 logger=None):
        self.name = name
        if logger:
            self._logger = logger
        else:
            import logging
            self._logger = logging.getLogger(__name__)
        self.service_name = service_name or os.path.basename(sys.argv[0])
        self.disc_client = disc_client
        self.bucketsize = 47
        self.delete_hndlr = delete_hndlr
        self.sem = Semaphore()
        self.disc_pub()
        gevent.sleep(1)
        self.workers = self.get_workers()
        self.partitions = {}
        self.pc = PartitionClient(self.service_name, self.name,
                                  list(self.workers), self.bucketsize,
                                  self.notify_hndlr, zookeeper)
        gevent.sleep(0)

    def disc_pub(self):
        self.disc_client.publish(self.service_name, dict(name=self.name))

    def get_workers(self):
        try:
            a = self.disc_client.subscribe(self.service_name, 0)
            gevent.sleep(0)
            self._dscvrd_workers = a.read()
            self._logger.debug('@workers(discovery):%s' % (' '.join(
                    map(lambda x: x['name'], self._dscvrd_workers))))
        except Exception as e:
            traceback.print_exc()
            self._logger.exception('@workers(discovery):%s\n%s' % (str(
                                                    e), str(dir(e))))
            self._dscvrd_workers = []
        return set(map(lambda x: x['name'], self._dscvrd_workers))

    def notify_hndlr(self, partitions):
        with self.sem:
            deleted = set(self.partitions.keys()) - set(partitions)
            if callable(self.delete_hndlr):
                self.delete_hndlr(map(lambda x: x.name, self._objects({
                        k: self.partitions[k] for k in deleted
                        })))
            for k in deleted:
                del self.partitions[k]
            for k in set(partitions) - set(self.partitions.keys()):
                self.partitions[k] = []
            self._logger.debug('@notify_hndlr partitions:%s' % ' '.join(
                        map(str, partitions)))

    def _objects(self, d={}):
        return list(set(sum(d.values(), [])))

    def work_items_itr(self):
        for i in self.work_items():
            yield i

    def work_items(self):
        return self._objects(self.partitions)

    def populate_work_items(self, items):
        self.refreash_work_items()
        with self.sem:
            for i in items:
                part = self.device2partition(i.name)
                if self.pc.own_partition(part):
                    if part not in self.partitions:
                        self.partitions[part] = []
                    if i.name not in map(lambda x: x.name,
                                         self.partitions[part]):
                        self.partitions[part].append(i)
        self._logger.debug('@populate_work_items(%s): done!' % ' '.join(
                map(lambda v: str(v[0]) + ':' + ','.join(map(
                        lambda x: x.name, v[1])), self.partitions.items())))
        gevent.sleep(0)

    def device2partition(self, key):
        return struct.unpack('Q', hashlib.md5(key).digest(
                    )[-8:])[0] % self.bucketsize

    def refreash_work_items(self):
        for k in self.partitions:
            self.partitions[k] = []

    def refresh_workers(self):
        w = self.get_workers()
        if self.workers != w:
            try:
                self._logger.debug('@refresh_workers updt(%s|%s)' % (
                        ' '.join(w), ' '.join(self.workers)))
                self.pc.update_cluster_list(list(w))
                gevent.sleep(0)
                self.workers = w
            except Exception as e:
                traceback.print_exc()
                self._logger.exception('@run(libpartition):%s' % str(e))
        self._logger.debug('@refresh_workers(%s): done!' % ' '.join(w))
        gevent.sleep(0)
