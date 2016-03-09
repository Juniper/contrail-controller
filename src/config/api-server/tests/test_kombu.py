import gevent
from gevent import monkey
monkey.patch_all()
import kombu
import sys

import unittest
from flexmock import flexmock
from cfgm_common import vnc_kombu
from vnc_cfg_api_server import vnc_cfg_ifmap
from distutils.version import LooseVersion


if LooseVersion(kombu.__version__) >= LooseVersion("2.5.0"):
    is_kombu_client_v1 = False
else:
    is_kombu_client_v1 = True


class CorrectValueException(Exception):
    pass


class WrongValueException(Exception):
    pass


class TestIfmapKombuClient(unittest.TestCase):
    def setUp(self):
        self.mock_connect = flexmock(operational = True,
            _info = lambda: "",
            connection_errors = (Exception,),
            channel_errors = ()
            )
        self.port = 5672
        self.username = "guest"
        self.password = "contrail123"
        self.vhost = "vhost0"
        self.db_client_mgr = flexmock(operational=True, _sandesh=None,
                       config_log=lambda *args, **kwargs: None,
                       get_server_port=lambda: 8082)
        self._url_template = "pyamqp://%s:%s@%s:%d/%s"
        self.mock_producer = flexmock(operational = True)
        self.mock_consumer = flexmock(operational = True)
        flexmock(vnc_kombu.kombu.Connection, __new__ = lambda *args, **kwargs: self.mock_connect)
        flexmock(vnc_kombu.kombu.Producer, __new__ = lambda *args, **kwargs: self.mock_producer)
        flexmock(vnc_kombu.kombu.Consumer, __new__= lambda *args, **kwargs: self.mock_consumer)

    def _url(self, server,
             port=None,
             username=None,
             password=None):
        port = self.port if not port else port
        username = self.username if not username else username
        password = self.password if not password else password
        return self._url_template % (username, password, server, port, self.vhost)

    @unittest.skipIf(is_kombu_client_v1,
                     "skipping because kombu client is older")
    def test_url_parsing(self):
        check_value = []
        def Connection(self, urls):
            if set(urls) != set(check_value):
                raise WrongValueException("expected %s - received %s", str(check_value), str(urls))
            else:
                raise CorrectValueException()

        flexmock(vnc_kombu.kombu.Connection, __new__ = Connection)

        servers = "a.a.a.a,b.b.b.b:5050,cccc@c.c.c.c,dddd@d.d.d.d:5050,eeee:xxxx@e.e.e.e,ffff:xxxx@f.f.f.f:5050"
        check_value = [self._url("a.a.a.a"),
                 self._url("b.b.b.b", port=5050),
                 self._url("c.c.c.c", username="cccc"),
                 self._url("d.d.d.d", port=5050, username="dddd"),
                 self._url("e.e.e.e", username="eeee", password="xxxx"),
                 self._url("f.f.f.f", username="ffff", password="xxxx", port=5050)]
        with self.assertRaises(CorrectValueException):
            vnc_cfg_ifmap.VncServerKombuClient(self.db_client_mgr,
                                         servers, self.port,
                                         None, self.username,
                                         self.password,
                                         self.vhost, False)

    @unittest.skipIf(is_kombu_client_v1,
                     "skipping because kombu client is older")
    def test_connection_monitor(self):
        flexmock(self.mock_connect).should_receive("close").twice()
        flexmock(self.mock_connect).should_receive("connect").twice()
        flexmock(self.mock_connect).should_receive("ensure_connection").twice()
        flexmock(self.mock_connect).should_receive("channel").twice()
        flexmock(self.db_client_mgr).should_receive("wait_for_resync_done"). \
            with_args().once()
        flexmock(self.mock_consumer).should_receive("consume").twice()

        _lock = gevent.lock.Semaphore()
        _lock.acquire()
        def _drain_events():
            if _lock.locked():
                _lock.release()
                raise Exception()
            else:
                gevent.sleep(5)
            return

        flexmock(self.mock_connect).should_receive("drain_events").replace_with(_drain_events).twice()
        servers = "a.a.a.a"
        kc = vnc_cfg_ifmap.VncServerKombuClient(self.db_client_mgr,
                                     servers, self.port,
                                     None, self.username,
                                     self.password,
                                     self.vhost, False)
        _lock.wait()

    @unittest.skipIf(is_kombu_client_v1,
                     "skipping because kombu client is older")
    def test_connection_publish(self):
        flexmock(self.mock_connect).should_receive("close").twice()
        flexmock(self.mock_connect).should_receive("connect").twice()
        flexmock(self.mock_connect).should_receive("ensure_connection").twice()
        flexmock(self.mock_connect).should_receive("channel").twice()
        flexmock(self.db_client_mgr).should_receive("wait_for_resync_done"). \
            with_args().once()
        flexmock(self.mock_consumer).should_receive("consume").twice()

        _lock = gevent.lock.Semaphore()
        _lock.acquire()
        def _drain_events():
            gevent.sleep(1000)
            return

        req_id = []
        def _publish(args):
            req_id.append(args['request-id'])
            if _lock.locked():
                _lock.release()
                raise Exception()
            return

        flexmock(self.mock_connect).should_receive("drain_events").replace_with(_drain_events).once()
        flexmock(self.mock_producer).should_receive("publish").replace_with(_publish).twice()
        servers = "a.a.a.a"
        kc = vnc_cfg_ifmap.VncServerKombuClient(self.db_client_mgr,
                                     servers, self.port,
                                     None, self.username,
                                     self.password,
                                     self.vhost, False)
        gevent.sleep(0)
        kc.dbe_create_publish("network", [], {})
        _lock.wait()

        # check if message is not missed out by publish error
        self.assertEqual(len(req_id), 2)
        self.assertEqual(len(set(req_id)), 1)

