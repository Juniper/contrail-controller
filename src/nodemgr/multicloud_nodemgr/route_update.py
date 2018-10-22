from collections import deque, namedtuple
import ctypes
import ctypes.util
from threading import Thread
from time import time

from pyroute2 import IPRoute

libc = ctypes.CDLL(ctypes.util.find_library('c'))

RouteUpdate = namedtuple('RouteUpdate', ('timestamp', 'operation', 'target', 'via', 'interface'))


class IPRouteWatcher():
    def __init__(self):
        self.messages = deque(maxlen=200)

    def listen(self):
        thread = Thread(target=self._listen)
        thread.daemon = True
        thread.start()

    @property
    def new_messages(self):
        return self.messages

    def _listen(self):
        with IPRoute() as ipr:
            ipr.bind()
            while True:
                for message in ipr.get():
                    update = self.process_update(message)
                    if update:
                        self.messages.append(update)

    @staticmethod
    def process_update(update):
        op = None
        if update['event'] == 'RTM_NEWROUTE':
            op = 'Add'
        elif update['event'] == 'RTM_DELROUTE':
            op = 'Delete'
        else:
            return None

        timestamp = int(time())
        target = '{}/{}'.format(update.get_attr('RTA_DST'), update['dst_len'])
        via = update.get_attr('RTA_GATEWAY')
        interface = IPRouteWatcher.index_to_name(update.get_attr('RTA_OIF'))

        return RouteUpdate(
            timestamp=timestamp, operation=op,
            target=target, via=via, interface=interface)

    @staticmethod
    def index_to_name(index):
        if not isinstance(index, int):
            raise TypeError('Interface index must be an int.')

        libc.if_indextoname.argtypes = [ctypes.c_uint32, ctypes.c_char_p]
        libc.if_indextoname.restype = ctypes.c_char_p

        interface_name = ctypes.create_string_buffer(32)
        interface_name = libc.if_indextoname(index, interface_name)

        if not interface_name:
            raise RuntimeError("Invalid interface index")

        return interface_name
