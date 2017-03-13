#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from gevent.queue import Queue as GQueue
from gevent.lock import Semaphore
import os, json, sys, subprocess, time, gevent, socket
from tempfile import NamedTemporaryFile, mkdtemp
import cPickle as pickle
from snmpuve import SnmpUve
from opserver.consistent_schdlr import ConsistentScheduler
from device_config import DeviceConfig, DeviceDict
import ConfigParser
import signal
import random
import hashlib
from sandesh.snmp_collector_info.ttypes import SnmpCollectorInfo, \
    SnmpCollectorUVE

class MaxNinTtime(object):
    def __init__(self, n, t, default=0):
        self._n = n
        self._t = t
        self._default = default
        self._slots = [0] * self._n
        self._pointer = 0

    def add(self):
        rt = self._default
        t = time.time()
        diff = t - self._slots[self._pointer]
        if diff < self._t:
            rt = self._t - diff
        self._add(t)
        return rt

    def _add(self, t):
        self._slots[self._pointer] = t
        self._pointer += 1
        self._pointer %= self._n

    def ready4full_scan(self):
        t = time.time()
        diff = t - self._slots[self._pointer - 1]
        if diff >= self._t:
            self._add(t)
            return True
        return False

class Controller(object):
    def __init__(self, config):
        self._config = config
        self._config.random_collectors = self._config.collectors()
        self._chksum = ""
        if self._config.collectors():
             self._chksum = hashlib.md5("".join(self._config.collectors())).hexdigest()
             self._config.random_collectors = random.sample(self._config.collectors(), \
                                                            len(self._config.collectors()))
        self.uve = SnmpUve(self._config)
        self._hostname = socket.gethostname()
        self._logger = self.uve.logger()
        self.sleep_time()
        self._keep_running = True
        self.last = set()
        self._sem = None
        self._config.set_cb(self.notify)
        self._mnt = MaxNinTtime(3, self._sleep_time)
        self._state = 'full_scan' # replace it w/ fsm
        self._if_data = None # replace it w/ fsm
        self._cleanup = None
        self._members = None
        self._partitions = None
        self._prouters = None

    def _make_if_cdata(self, data):
        if_cdata = {}
        t = time.time()
        for dev in data:
            if 'snmp' in data[dev]:
                if 'ifMib' in data[dev]['snmp']:
                    if 'ifTable' in data[dev]['snmp']['ifMib']:
                        if_cdata[dev] = dict(map(lambda x: (
                                x['ifIndex'], (x['ifOperStatus'], t)),
                                        filter(lambda x: 'ifOperStatus' in x\
                                            and 'ifDescr' in x, data[dev][
                                               'snmp']['ifMib']['ifTable'])))
                elif 'ifOperStatus' in data[dev]['snmp']:
                    if_cdata[dev] = dict((k, (v, t)) for k, v in
                                    data[dev]['snmp']['ifOperStatus'].items())
        return if_cdata

    def _set_status(self, _dict, dev, intf, val):
        if dev not in _dict:
            _dict[dev] = {}
        _dict[dev][intf] = val

    def _check_and_update_ttl(self, up2down):
        t = time.time()
        expry = 3 * self._fast_scan_freq
        for dev in self._if_data:
            for intf in self._if_data[dev]:
                if self._if_data[dev][intf][0] == 1:
                    if t - self._if_data[dev][intf][1] > expry:
                        self._set_status(up2down, dev, intf, 7) #no resp
                        self._if_data[dev][intf] = (7, t)

    def _get_if_changes(self, if_cdata):
        down2up, up2down, others = {}, {}, {}
        for dev in if_cdata:
            if dev in self._if_data:
                for intf in if_cdata[dev]:
                    if intf in self._if_data[dev]:
                        if if_cdata[dev][intf][0] != self._if_data[dev][
                                intf][0]:
                            if self._if_data[dev][intf][0] == 1:
                                self._set_status(up2down, dev, intf,
                                                 if_cdata[dev][intf][0])
                            elif if_cdata[dev][intf][0] == 1:
                                self._set_status(down2up, dev, intf,
                                                 if_cdata[dev][intf][0])
                            else:
                                self._set_status(others, dev, intf,
                                                 if_cdata[dev][intf][0])
                    self._if_data[dev][intf] = if_cdata[dev][intf]
            else:
                self._if_data[dev] = if_cdata[dev]
                for intf in self._if_data[dev]:
                    if self._if_data[dev][intf][0] == 1:
                        self._set_status(down2up, dev, intf,
                                         if_cdata[dev][intf][0])
                    else:
                        self._set_status(others, dev, intf,
                                         if_cdata[dev][intf][0])
        return down2up, up2down, others

    def _chk_if_change(self, data):
        if_cdata = self._make_if_cdata(data)
        down2up, up2down, others = self._get_if_changes(if_cdata)
        self._check_and_update_ttl(up2down)
        self._logger.debug('@chk_if_change: down2up(%s), up2down(%s), ' \
                'others(%s)' % (', '.join(down2up.keys()),
                                ', '.join(up2down.keys()),
                                ', '.join(others.keys())))
        return down2up, up2down, others

    def _extra_call_params(self):
        if self._state != 'full_scan':
            return dict(restrict='ifOperStatus')
        return {}

    def _analyze(self, data):
        ret = True
        time = self._fast_scan_freq
        if self._state != 'full_scan':
            down2up, up2down, others = self._chk_if_change(data)
            if down2up:
                self._state = 'full_scan'
                time = self._mnt.add()
            elif self._mnt.ready4full_scan():
                self._state = 'full_scan'
                time = 0
            elif up2down:
                self.uve.send_ifstatus_update(self._if_data)
            ret = False
            sret = 'chngd: ' + self._state + ', time: ' + str(time)
        else:
            self._state = 'fast_scan'
            self._if_data = self._make_if_cdata(data)
            self.uve.send_ifstatus_update(self._if_data)
            sret = 'chngd: %d' % len(self._if_data)
        self._logger.debug('@do_work(analyze):State %s(%d)->%s!' % (
                    self._state, len(data), str(sret)))
        return ret, time

    def notify(self, svc, msg='', up=True, servers=''):
        self.uve.conn_state_notify(svc, msg, up, servers)

    def stop(self):
        self._keep_running = False

    def sleep_time(self, newtime=None):
        if newtime:
            self._sleep_time = newtime
        else:
            self._sleep_time = self._config.frequency()
        self._fast_scan_freq = self._config.fast_scan_freq()
        if self._fast_scan_freq > self._sleep_time / 2:
            self._fast_scan_freq = self._sleep_time / 2
        return self._sleep_time

    def _setup_io(self):
        cdir = mkdtemp()
        input_file = os.path.join(cdir, 'in.data')
        output_file = os.path.join(cdir, 'out.data')
        return cdir, input_file, output_file

    def _create_input(self, input_file, output_file, devices, i, restrict=None):
        if isinstance(devices[0], DeviceDict):
            devices = DeviceConfig.populate_cfg(devices)
        with open(input_file, 'wb') as f:
            data = dict(out=output_file,
                        netdev=devices,
                        instance=i)
            if restrict:
                data['restrict'] = restrict
            pickle.dump(data, f)
            f.flush()

    def _run_scanner(self, input_file, output_file, i):
        proc = subprocess.Popen('contrail-snmp-scanner --input %s' % (
                    input_file), shell=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                close_fds=True)
        self._cleanup = (proc, output_file)
        o,e = proc.communicate()
        self._cleanup = None
        self._logger.debug('@run_scanner(%d): scan done with %d\nstdout:' \
                '\n%s\nstderr:\n%s\n' % (i, proc.returncode, o, e))
        with open(output_file, 'rb') as f:
            d = pickle.load(f)
        self._logger.debug('@run_scanner(%d): loaded %s' % (i, output_file))
        return d

    def _cleanup_io(self, cdir, input_file, output_file):
        os.unlink(input_file)
        os.unlink(output_file)
        os.rmdir(cdir)

    def _send_uve(self, d):
        for dev, data in d.items():
            if dev:
                self.uve.send(data['snmp'])
                self.uve.send_flow_uve({'name': dev,
                    'flow_export_source_ip': data['flow_export_source_ip']})
                self.find_fix_name(data['name'], dev)
        self._logger.debug('@send_uve:Processed %d!' % (len(d)))

    def _send_snmp_collector_uve(self, members, partitions, prouters):
        snmp_collector_info = SnmpCollectorInfo()
        if self._members != members:
            self._members = members
            snmp_collector_info.members = members
        if self._partitions != partitions:
            self._partitions = partitions
            snmp_collector_info.partitions = partitions
        if self._prouters != prouters:
            self._prouters = prouters
            snmp_collector_info.prouters = prouters
        if snmp_collector_info != SnmpCollectorInfo():
            snmp_collector_info.name = self._hostname
            SnmpCollectorUVE(data=snmp_collector_info).send()
    # end _send_snmp_collector_uve

    def _del_uves(self, l):
        with self._sem:
            for dev in l:
                self.uve.delete(dev)

    def do_work(self, i, devices):
        self._logger.debug('@do_work(%d):started (%d)...' % (i, len(devices)))
        sleep_time = self._fast_scan_freq
        if devices:
            with self._sem:
                self._work_set = devices
                cdir, input_file, output_file = self._setup_io()
                self._create_input(input_file, output_file, devices,
                                   i, **self._extra_call_params())
                data = self._run_scanner(input_file, output_file, i)
                self._cleanup_io(cdir, input_file, output_file)
                do_send, sleep_time = self._analyze(data)
                if do_send:
                    self._send_uve(data)
                    gevent.sleep(0)
                del self._work_set
        self._logger.debug('@do_work(%d):Processed %d!' % (i, len(devices)))
        return sleep_time

    def find_fix_name(self, cfg_name, snmp_name):
        if snmp_name != cfg_name:
            self._logger.debug('@find_fix_name: snmp name %s differs from ' \
                    'configured name %s, fixed for this run' % (
                            snmp_name, cfg_name))
            for d in self._work_set:
                if d.name == cfg_name:
                    d.name = snmp_name
                    return

    def sighup_handler(self):
        if self._config._args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(self._config._args.conf_file)
            if 'DEFAULTS' in config.sections():
                try:
                    collectors = config.get('DEFAULTS', 'collectors')
                    if type(collectors) is str:
                        collectors = collectors.split()
                        new_chksum = hashlib.md5("".join(collectors)).hexdigest()
                        if new_chksum != self._chksum:
                            self._chksum = new_chksum
                            random_collectors = random.sample(collectors, len(collectors))
                            self.uve.sandesh_reconfig_collectors(random_collectors)
                except ConfigParser.NoOptionError as e: 
                    pass
    # end sighup_handler  

    def run(self):
       
        """ @sighup
        SIGHUP handler to indicate configuration changes
        """
        gevent.signal(signal.SIGHUP, self.sighup_handler) 

        i = 0
        self._sem = Semaphore()
        self._logger.debug('Starting.. %s' % str(
                    self._config.zookeeper_server()))
        constnt_schdlr = ConsistentScheduler(
                            self._config._name,
                            zookeeper=self._config.zookeeper_server(),
                            delete_hndlr=self._del_uves,
                            logger=self._logger, 
                            cluster_id=self._config.cluster_id())
        while self._keep_running:
            self._logger.debug('@run: ittr(%d)' % i)
            if constnt_schdlr.schedule(self._config.devices()):
                members = constnt_schdlr.members()
                partitions = constnt_schdlr.partitions()
                prouters = map(lambda x: x.name, constnt_schdlr.work_items())
                self._send_snmp_collector_uve(members, partitions, prouters)
                sleep_time = self.do_work(i, constnt_schdlr.work_items())
                self._logger.debug('done work %s' % str(prouters))
                i += 1
                gevent.sleep(sleep_time)
            else:
                gevent.sleep(1)
        constnt_schdlr.finish()
