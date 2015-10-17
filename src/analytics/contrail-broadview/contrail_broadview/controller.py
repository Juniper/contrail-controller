#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from gevent import monkey
monkey.patch_all()
from client import BroadviewApiClient
import time, socket, os
from bv_uve import BroadViewOL
import gevent
from gevent.coros import Semaphore
from opserver.consistent_schdlr import ConsistentScheduler
from bottle import Bottle, request, run
import json

class PRouter(object):
    def __init__(self, name, data):
        self.name = name
        self.data = data

class Controller(object):
    def __init__(self, config):
        self._config = config
        self._me = socket.gethostname() + ':' + str(os.getpid())
        self.ol = BroadViewOL(self._config)
        self.bv_api = BroadviewApiClient(self.ol)
        self.sleep_time()
        self._keep_running = True
        self.prouters = None

    def stop(self):
        self._keep_running = False

    def sleep_time(self, newtime=None):
        if newtime:
            self._sleep_time = newtime
        else:
            self._sleep_time = self._config.frequency()
        return self._sleep_time

    def get_prouters(self):
        self.prouters = self._config.get_prouters()

    def compute(self):
        t = []
        # for prouter in self.constnt_schdlr.work_items():
        for prouter in self.prouters:
            t.append(gevent.spawn(self.get_prouter_bview, prouter))
        gevent.joinall(t)

    def get_prouter_bview(self, prouter):
        self.bv_api.update_prouter(prouter)
        for asic in prouter.asics():
            import pdb; pdb.set_trace()
            d = self.bv_api.get_bst_report(prouter, asic)
            self._send_report(prouter, d)

    def _send_report(self, prouter, d):
        if 'report' in d:
            data = dict(name=prouter.name(), asic_id=d['asic-id'])
            for realm in d['report']:
                rn = self.ol.map_realm_name(realm['realm'])
                if rn:
                    nf = getattr(self, 'extract_' + rn, None)
                    # print 'extract_' + rn, nf
                    if callable(nf):
                        # print 'called extract_' + rn, realm
                        nf(data, realm)
            self.send_ol(data)

    def send_ol(self, d):
        self.ol.send(d)

    def switcher(self):
        gevent.sleep(0)

    def scan_data(self):
        t = []
        t.append(gevent.spawn(self.get_prouters))
        gevent.joinall(t)

    def _del_ol(self, prouters):
        with self._sem:
            for prouter in prouters:
                self.ol.delete(prouter.name())

    def run(self):
        self.run_async()

    def run_async(self):
        app = Bottle()
        app.route('/agent_response',  method='POST')(self.rcv_msg)
        run(app, host='0.0.0.0', port=9814, server='gevent')

    def rcv_msg(self):
        remote = self._config.get_remote_info(request.remote_addr)
        data = json.loads(request.body.read())
        # header = dict(request.headers.items())
        self._send_report(remote, data)

    def run_poll(self):
        self._sem = Semaphore()
        #self.constnt_schdlr = ConsistentScheduler(
        #                    self.ol._moduleid,
        #                    zookeeper=self._config.zookeeper_server(),
        #                    delete_hndlr=self._del_ol)
        while self._keep_running:
            self.scan_data()
            # if self.constnt_schdlr.schedule(self.prouters):
            if self.prouters:
                print '@run: ', self.prouters, self._keep_running
                try:
                    with self._sem:
                        self.compute()
                except Exception as e:
                    import traceback; traceback.print_exc()
                    print str(e)
                gevent.sleep(self._sleep_time)
            else:
                gevent.sleep(1)
        # self.constnt_schdlr.finish()

    def extract_device(self, dest, raw):
        dest['device'] = raw['data']

    def extract_ingressPortPriorityGroup(self, dest, raw):
        dest['ingressPortPriorityGroup'] = []
        for d in raw['data']:
            for dp in d['data']:
                dest['ingressPortPriorityGroup'].append(dict(
                    port=d['port'], priorityGroup=dp[0],
                    umShareBufferCount=dp[1],
                    umHeadroomBufferCount=dp[2]))

    def extract_ingressPortServicePool(self, dest, raw):
        print raw
        dest['ingressPortServicePool'] = []
        for d in raw['data']:
            for dp in d['data']:
                dest['ingressPortServicePool'].append(dict(
                    port=d['port'],
                    servicePool=dp[0],
                    umShareBufferCount=dp[1]))

    def extract_ingressServicePool(self, dest, raw):
        dest['ingressServicePool'] = map(lambda x: dict(
                    servicePool=x[0],
                    umShareBufferCount=x[1]), raw['data'])

    def extract_egressPortServicePool(self, dest, raw):
        dest['egressPortServicePool'] = []
        for d in raw['data']:
            for dp in d['data']:
                dest['egressPortServicePool'].append(dict(
                    port=d['port'],
                    servicePool=dp[0],
                    ucShareBufferCount=dp[1],
                    umShareBufferCount=dp[2],
                    mcShareBufferCount=dp[3]))
                    #mcShareQueueEntries=x['data'][4]), raw['data'])

    def extract_egressServicePool(self, dest, raw):
        dest['egressServicePool'] = map(lambda x: dict(
                servicePool=x[0],
                umShareBufferCount=x[1],
                mcShareBufferCount=x[2],
                mcShareQueueEntries=x[3]), raw['data'])

    def extract_egressUcQueue(self, dest, raw):
        dest['egressUcQueue'] = map(lambda x: dict(
                queue=x[0],
                ucBufferCount=x[2]), raw['data'])

    def extract_egressUcQueueGroup(self, dest, raw):
        dest['egressUcQueueGroup'] = map(lambda x: dict(
                queueGroup=x['data'][0],
                ucBufferCount=x['data'][1]), raw['data'])

    def extract_egressMcQueue(self, dest, raw):
        dest['egressMcQueue'] = map(lambda x: dict(
                queue=x['data'][0],
                ucBufferCount=x['data'][1],
                mcQueueEntries=x['data'][2]), raw['data'])

    def extract_egressCpuQueue(self, dest, raw):
        dest['egressCpuQueue'] = map(lambda x: dict(
                queue=x['data'][0],
                cpuBufferCount=x['data'][1]), raw['data'])

    def extract_egressRqeQueue(self, dest, raw):
        dest['egressRqeQueue'] = map(lambda x: dict(
                queue=x['data'][0],
                rqeBufferCount=x['data'][1]), raw['data'])

