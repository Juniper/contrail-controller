import sys
import os
import ConfigParser

def parse_contrail_dns_conf():

    named_defaults = {
        'named_config_file': 'DEFAULT....contrail-named.conf',
        'named_config_directory': '/etc/contrail/dns',
        'named_log_file': 'DEFAULT.../var/log/contrail/contrail-named.log',
        'rndc_config_file': 'contrail-rndc.conf',
        'rndc_secret': 'xvysmOR8lnUQRBcunkC6vg==',
        'named_max_cache_size': '32M',
        'named_max_retransmissions': '12',
        'named_retransmission_interval': '1000',
    }

    # parse contrail-dns.conf
    dns_config = ConfigParser.SafeConfigParser()
    dns_config.read('contrail-dns.conf')

    print '\n ** Print dict before parsing **\n'
    for key,value in named_defaults.items():
        print (key,value)
    # update defaults
    named_defaults.update(dict(dns_config.items("DEFAULT")))

    print '\n ** Update named_defaults removing comments **\n'
    for key,value in named_defaults.items():
        print (key,value)
        split_list = value.split(' #')
        if len(split_list) == 2:
            val, comment = split_list
            val = val.replace(' ', '')
            print '\n Value, Comment'
            print (val, comment)
            named_defaults[key]=val
        
    print '\n ** Print dict after furhtur parsing **\n'
    for key,value in named_defaults.items():
        print (key,value)

    # create contrail-named-base.conf
    file_named_base_conf=open('contrail-named-base.conf', 'w+')

    # build contrail-named-base.conf
    file_named_base_conf.write("/* Starting to Build contrail-named-base.conf */ \n")

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
    file_named_base_conf.write('    max-cache-size "'+named_defaults['named_max_cache_size'] + '";\n')    
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
    file_named_base_conf.write('        file "'+ named_defaults['named_log_file'] + '" version 5 size 5m;\n')
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
    print '\n ******* Executing applynamedconfig.py ***** \n'
    if not os.path.exists('contrail-named-base.conf'):
        # parse contrail-dns.conf and build contrail-named-base.conf
        parse_contrail_dns_conf()
    else:
        print '**** contrail-named-base.conf file exists ******'

    # open contrail-named-base.conf
    file1=open('contrail-named-base.conf', 'r')
    file1_contents = file1.read()

    # open contrail-named.conf to seek the view stanza
    file2_contents = "" 
    file2 = open('contrail-named.conf', 'r')
    last_pos = file2.tell()
    for line in file2:
        if 'view' in line:
            file2.seek(last_pos)
            file2_contents = file2.read()
            break
        last_pos = file2.tell()

    # rewrite contrail-named.conf concatening
    # contrail-named-base.conf and contrail-named.conf from view stanza
    file3 = open('contrail-named.conf', 'w')
    file3.truncate()
    file3.write(file1_contents + file2_contents)
    file3.close()

    file1.close()
    file2.close()

    # apply config
    os.system('/usr/bin/contrail-rndc -c /etc/contrail/dns/contrail-rndc.conf reconfig')
#end main

if __name__ == "__main__":
    main()
