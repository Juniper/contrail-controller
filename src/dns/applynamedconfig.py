#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This helper script is used to dynamically update configs to named.
o For backward compatibility we read the named configurable
   params from contrail-dns.conf and build contrail-named-base.conf
o Alternatively user can create/update contrail-name-base.conf
   for configuring named params
o contrail-named.conf will be generated by dnsd which will
   contain views/zones stanzas
o contrail-named-base.conf will be merged with contrail-named.conf
   by the script and config applied to named
"""

import sys
import os
import subprocess
import ConfigParser

def parse_contrail_dns_conf():

    named_defaults = {
        'named_config_file': 'contrail-named.conf',
        'named_config_directory': '/etc/contrail/dns',
        'named_log_file': '/var/log/contrail/contrail-named.log',
        'rndc_config_file': 'contrail-rndc.conf',
        'rndc_secret': 'xvysmOR8lnUQRBcunkC6vg==',
        'named_max_cache_size': '32M',
        'named_max_retransmissions': '12',
        'named_retransmission_interval': '1000',
    }

    # remove preceeding spaces from contrail-dns.conf
    # and save it in contrail-dns-temp.conf
    subprocess.call(["sed -e 's/^[ \t]*//g' < /etc/contrail/contrail-dns.conf \
                                            > /etc/contrail/dns/contrail-dns-temp.conf"], shell=True)
    # remove comments preceeding with #
    subprocess.call(["sed -i 's/[;#].*$//g' /etc/contrail/dns/contrail-dns-temp.conf"], shell=True)

    # parse contrail-dns.conf
    dns_config = ConfigParser.SafeConfigParser()
    dns_config.read('/etc/contrail/dns/contrail-dns-temp.conf')

    # remove the temp file
    os.remove("/etc/contrail/dns/contrail-dns-temp.conf")

    # update defaults
    named_defaults.update(dict(dns_config.items("DEFAULT")))

    # create contrail-named-base.conf
    file_named_base_conf=open('/etc/contrail/dns/contrail-named-base.conf', 'w+')

    # build contrail-named-base.conf

    # build options {} stanza
    file_named_base_conf.write('options {\n')
    file_named_base_conf.write('    directory "'+ named_defaults['named_config_directory'] + '";\n')
    file_named_base_conf.write('    managed-keys-directory "'+ named_defaults['named_config_directory'] + '";\n')
    file_named_base_conf.write('    empty-zones-enable no;\n')
    file_named_base_conf.write('    pid-file "/etc/contrail/dns/contrail-named.pid";\n')
    file_named_base_conf.write('    session-keyfile "/etc/contrail/dns/session.key";\n')
    file_named_base_conf.write('    listen-on port 53 { any; };\n')
    file_named_base_conf.write('    allow-query { any; };\n')
    file_named_base_conf.write('    allow-recursion { any; };\n')
    file_named_base_conf.write('    allow-query-cache { any; };\n')
    file_named_base_conf.write('    max-cache-size '+ named_defaults['named_max_cache_size'] + ';\n')
    file_named_base_conf.write('};\n\n')

    # build rndc-key {} stanza
    file_named_base_conf.write('key "rndc-key" {\n')
    file_named_base_conf.write('   algorithm hmac-md5;\n')
    file_named_base_conf.write('   secret "' + named_defaults['rndc_secret'] + '";\n')
    file_named_base_conf.write('};\n\n')

    #build controls {} stanza
    file_named_base_conf.write('controls {\n')
    file_named_base_conf.write('    inet 127.0.0.1 port 8094 \n')
    file_named_base_conf.write('    allow { 127.0.0.1; }  keys { "rndc-key"; };\n')
    file_named_base_conf.write('};\n\n')

    #build logging {} stanza
    file_named_base_conf.write('logging {\n')
    file_named_base_conf.write('    channel debug_log {\n')
    file_named_base_conf.write('        file "'+ named_defaults['named_log_file'] + '" versions 5 size 5m;\n')
    file_named_base_conf.write('        severity debug;\n')
    file_named_base_conf.write('        print-time yes;\n')
    file_named_base_conf.write('        print-severity yes;\n')
    file_named_base_conf.write('        print-category yes;\n')
    file_named_base_conf.write('    };\n')
    file_named_base_conf.write('    category default {\n')
    file_named_base_conf.write('        debug_log;\n')
    file_named_base_conf.write('    };\n')
    file_named_base_conf.write('    category queries {\n')
    file_named_base_conf.write('        debug_log;\n')
    file_named_base_conf.write('    };\n')
    file_named_base_conf.write('};\n\n')

    file_named_base_conf.close()

# end parse_contrail_dns_conf


def main():
    if not os.path.exists('/etc/contrail/dns/contrail-named-base.conf'):
        # parse contrail-dns.conf and build contrail-named-base.conf
        parse_contrail_dns_conf()

    # open contrail-named-base.conf and read the base configs
    file1 = open('/etc/contrail/dns/contrail-named-base.conf', 'r')
    file1_lines = file1.readlines()
    file1.close()

    # open contrail-named.conf and remove configurable stanzas
    # options{} key{} controls{} logging {}
    count = 0
    file2 = open('/etc/contrail/dns/contrail-named.conf', 'r')
    lines = file2.readlines()
    for i, line in enumerate(lines[:]):
        if line.startswith('view'):
            break
        else:
           count = count + 1
    file2.close()

    # delete all lines before the view stanza {}
    del lines[1:count]

    # open contrail-named.conf
    file3 = open('/etc/contrail/dns/contrail-named.conf', 'w')
    file3.truncate()
    file3.write("/* Build from contrail-named-base.conf */\n")
    file3.writelines(file1_lines)
    file3.write("/* Build from contrail-named.conf */\n")
    file3.writelines(lines)
    file3.close()

    # apply config
    os.system('/usr/bin/contrail-rndc -c /etc/contrail/dns/contrail-rndc.conf reconfig')
#end main

if __name__ == "__main__":
    main()
