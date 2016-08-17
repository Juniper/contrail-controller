import sys
import os

# open contrail-named-base.conf
file1=open('contrail-named-base.conf', 'r')
file1_contents = file1.read()
file1.close()

# open contrail-named.conf
file2 = open('file2', 'r')
last_pos = file2.tell()
for line in file2:
    if 'view' in line:
        file2.seek(last_pos)
        file2_contents = file2.read()
        break
    last_pos = file2.tell()
file2.close()

# rewrite contrail-named.conf
file3 = open('contrail-named.conf', 'w')
file3.truncate()
file3.write(file1_contents + file2_contents)
file3.close()

# apply config
os.system('/usr/bin/contrail-rndc -c /etc/contrail/dns/contrail-rndc.conf reconfig')

