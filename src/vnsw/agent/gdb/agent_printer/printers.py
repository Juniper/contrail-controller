# Pretty-printers for libstc++.

# Copyright (C) 2008, 2009, 2010, 2011 Free Software Foundation, Inc.

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import gdb
import itertools
import re

# Try to use the new-style pretty-printing if available.
_use_gdb_pp = True
try:
    import gdb.printing
except ImportError:
    _use_gdb_pp = False

class TbbAtomicIntPrinter:
    "Print TBB atomic varaiable of some kind"
    def __init__ (self, typename, val):
        self.typename = typename
        self.val = val

    def to_string (self):
        return '(tbb::atomic) %s' % (self.val['my_storage']['my_value'])

class TbbConcurrentQueue:
    """Print TBB Concurrent Queue of some kind"""
    class Iterator:
        def __init__(self, my_rep, hc):
            self.my_rep = my_rep
            self.hc = hc
            self.element_type = self.my_rep.type.strip_typedefs().template_argument(0)
            k = 0
            self.array = []
            while k < self.my_rep['n_queue']:
                self.array.append(self.my_rep['array'][k]['head_page']['my_storage']['my_value'].dereference())
                k = k + 1
            self.count = 0

        def __iter__(self):
            return self

        def iter(self):
            return self

        def next(self):
            tc = self.my_rep['tail_counter']['my_storage']['my_value']
            if (self.hc == tc):
                raise StopIteration
            array_idx = int((self.hc * self.my_rep['phi']) % self.my_rep['n_queue'])
            pg_index = (self.hc / self.my_rep['n_queue']) & (self.my_rep['items_per_page'] - 1)
            if self.array[array_idx]['mask'] & ( 1 << pg_index):
                item = self.array[array_idx].cast(gdb.lookup_type('tbb::strict_ppl::internal::micro_queue< ' + str(self.element_type) + ' >::padded_page'))['last'].address[pg_index]
            else:
                item = None
            result = ('[%d]' % self.count, item)
            if (pg_index == (self.my_rep['items_per_page'] - 1)):
                self.array[array_idx] = self.array[array_idx]['next'].dereference()
            self.count = self.count + 1
            self.hc = self.hc + 1
            return result

        def __next__(self):
            return self.next()

    def __init__ (self, typename, val):
        self.typename = typename
        self.val = val
        self.my_rep = val['my_rep'].dereference()

    def get_size (self):
        hc = self.my_rep['head_counter']['my_storage']['my_value']
        nie = self.my_rep['n_invalid_entries']['my_storage']['my_value']
        tc = self.my_rep['tail_counter']['my_storage']['my_value']
        return (tc - hc - nie)

    def to_string (self):
        return '%s with %d elements' % (self.typename, self.get_size())

    def children (self):
        return self.Iterator(self.my_rep, self.my_rep['head_counter']['my_storage']['my_value'])

class MacAddressPrinter:
    "Print TBB atomic varaiable of some kind"
    def __init__ (self, typename, val):
        self.typename = typename
        self.val = val

    def to_string (self):
        oct = self.val['addr_']['ether_addr_octet']
        return '%02x:%02x:%02x:%02x:%02x:%02x' %(oct[0], oct[1], oct[2], oct[3], oct[4], oct[5])

class FlowKeyPrinter:
    "Print TBB atomic varaiable of some kind"
    def __init__ (self, typename, val):
        self.typename = typename
        self.val = val

    def to_string (self):
        nh = self.val['nh']
        family = self.val['family']
        proto = self.val['protocol']
        sport = self.val['src_port']
        dport = self.val['dst_port']
        sip = "FIXME"
        dip = "FIXME"
        if self.val['family'] == 1:
           family = 'IPv4'
           x = int(self.val['src_addr']['ipv4_address_']['addr_']['s_addr'])
           x1 = x & 0xFF
           x2 = (x & 0xFF00) >> 8
           x3 = (x & 0xFF0000) >> 16
           x4 = (x & 0xFF000000) >> 24
           sip = '%d.%d.%d.%d' %(x1, x2, x3, x4)
           x = int(self.val['dst_addr']['ipv4_address_']['addr_']['s_addr'])
           x1 = x & 0xFF
           x2 = (x & 0xFF00) >> 8
           x3 = (x & 0xFF0000) >> 16
           x4 = (x & 0xFF000000) >> 24
           dip = '%d.%d.%d.%d' %(x1, x2, x3, x4)
        else:
           family = 'IPv6'

        return '<%s nh=%-6d sip=%-15s dip=%-15s proto=%-3d sport=%-5d dport=%-5d>' % (family, nh, sip, dip, proto, sport, dport)

class IpPrinter:
    "Print TBB atomic varaiable of some kind"
    def __init__ (self, typename, val):
        self.typename = typename
        self.val = val

    def to_string (self):
        ip_type = ""
        addr = ""
        if self.val['type_'] == 0:
           x = int(self.val['ipv4_address_']['addr_']['s_addr'])
           x1 = x & 0xFF
           x2 = (x & 0xFF00) >> 8
           x3 = (x & 0xFF0000) >> 16
           x4 = (x & 0xFF000000) >> 24
           ip = '%d.%d.%d.%d' %(x1, x2, x3, x4)
           ip_type = "IPv4"
           addr = '%-15s' %(ip)
        else:
           addr = 'FIXME'

        return '<%s %-15s>' % (ip_type, addr)

class Ipv4Printer:
    "Print TBB atomic varaiable of some kind"
    def __init__ (self, typename, val):
        self.typename = typename
        self.val = val

    def to_string (self):
        addr = ""
        x = int(self.val['addr_']['s_addr'])
        x1 = x & 0xFF
        x2 = (x & 0xFF00) >> 8
        x3 = (x & 0xFF0000) >> 16
        x4 = (x & 0xFF000000) >> 24
        ip = '<%d.%d.%d.%d>' %(x1, x2, x3, x4)
        return '<%-15s>' %(ip)

# A "regular expression" printer which conforms to the
# "SubPrettyPrinter" protocol from gdb.printing.
class RxPrinter(object):
    def __init__(self, name, function):
        super(RxPrinter, self).__init__()
        self.name = name
        self.function = function
        self.enabled = True

    def invoke(self, value):
        if not self.enabled:
            return None
        return self.function(self.name, value)

# A pretty-printer that conforms to the "PrettyPrinter" protocol from
# gdb.printing.  It can also be used directly as an old-style printer.
class Printer(object):
    def __init__(self, name):
        super(Printer, self).__init__()
        self.name = name
        self.subprinters = []
        self.lookup = {}
        self.enabled = True
        self.compiled_rx = re.compile('^([a-zA-Z0-9_:]+)<.*>$')

    def add(self, name, function):
        # A small sanity check.
        # FIXME
        if not self.compiled_rx.match(name + '<>'):
            raise ValueError('libstdc++ programming error: "%s" does not match' % name)
        printer = RxPrinter(name, function)
        self.subprinters.append(printer)
        self.lookup[name] = printer

    @staticmethod
    def get_basic_type(type):
        # If it points to a reference, get the reference.
        if type.code == gdb.TYPE_CODE_REF:
            type = type.target ()

        # Get the unqualified type, stripped of typedefs.
        type = type.unqualified ().strip_typedefs ()

        return type.tag

    def __call__(self, val):
        typename = self.get_basic_type(val.type)
        if not typename:
            return None

        # All the types we match are template types, so we can use a
        # dictionary.
        match = self.compiled_rx.match(typename)
        if not match:
            basename = typename
        else:
            basename = match.group(1)

        if basename in self.lookup:
            return self.lookup[basename].invoke(val)

        # Cannot find a pretty printer.  Return None.
        return None

agent_printer = None
agent_printer_registered = False

def register_agent_printers (obj):
    "Register agent pretty-printers with objfile Obj."

    global _use_gdb_pp
    global agent_printer
    global agent_printer_registered

    if agent_printer_registered is True:
        return

    agent_printer_registered = True
    if _use_gdb_pp:
        gdb.printing.register_pretty_printer(obj, agent_printer)
    else:
        if obj is None:
            obj = gdb
        obj.pretty_printers.append(agent_printer)

def build_agent_dictionary ():
    global agent_printer

    agent_printer = Printer("agent-printer")

    # tbb objects requiring pretty-printing.
    # In order from:
    agent_printer.add('FlowKey', FlowKeyPrinter)
    agent_printer.add('boost::asio::ip::address', IpPrinter)
    agent_printer.add('boost::asio::ip::address_v4', Ipv4Printer)
    agent_printer.add('tbb::atomic', TbbAtomicIntPrinter)
    agent_printer.add('tbb::strict_ppl::concurrent_queue', TbbConcurrentQueue)
    agent_printer.add('tbb::concurrent_queue', TbbConcurrentQueue)
    agent_printer.add('MacAddress', MacAddressPrinter)
build_agent_dictionary ()
