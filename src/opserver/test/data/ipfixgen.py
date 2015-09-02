# This is sample code to generate udp packets
# from a pcap file, used for ipfix testing
import dpkt
import pcap
from dpkt.ip import IP
from dpkt.udp import UDP

idx = 0
for ts, raw_pkt in pcap.pcap('mydump'):
    ip = IP(raw_pkt[14:])
    udp = ip.data
    f = open('workfile'+str(idx), 'w')
    f.write(udp.data)
    f.close()
    idx = idx + 1
