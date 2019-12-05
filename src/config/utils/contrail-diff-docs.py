#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from builtins import object
import sys
import json

class DocDiff(object):

    def __init__(self, old, new):
        self._old_fpath = old
        self._new_fpath = new
    # end __init__

    def run(self):
        old_sdict = {}
        with open(self._old_fpath, "r") as ofp:
           old_sdict = json.loads(ofp.read())
        new_sdict = {}
        with open(self._new_fpath, "r") as nfp:
            new_sdict = json.loads(nfp.read())
        added, removed, modified, cmodified, smodified = \
            self._compare_message_dicts(old_sdict["messages"],
                                        new_sdict["messages"])
        if added or removed or modified:
            print('\n')
        if added:
            print('Added systemlog and objectlog messages:')
            for idx, mname in enumerate(added):
                print('    %d. %s' % (idx + 1, mname))
            print('\n')
        if removed:
            print('Removed systemlog and objectlog messages:')
            for idx, mname in enumerate(removed):
                print('    %d. %s' % (idx + 1, mname))
            print('\n')
        if modified:
            print('Modified systemlog and objectlog messages:')
            for idx, mname in enumerate(modified.keys()):
                if mname in cmodified:
                    print('    %d. %s: Contents' % (idx + 1, mname))
                else:
                    stuple = smodified[mname]
                    print('    %d. %s: Severity : [%s] -> [%s]' % \
                         (idx + 1, mname, stuple[0], stuple[1]))
            print('\n')
    # end run

    def _compare_message_dicts(self, old_mdict, new_mdict):
        old_mnames = set(old_mdict.keys())
        new_mnames = set(new_mdict.keys())
        intersect_mnames = old_mnames.intersection(new_mnames)
        added_messages = new_mnames - old_mnames
        removed_messages = old_mnames - new_mnames
        modified_messages = { mname : (old_mdict[mname], new_mdict[mname]) \
            for mname in intersect_mnames \
            if old_mdict[mname] != new_mdict[mname] }
        content_modified_messages = [ mname for mname in \
            list(modified_messages.keys()) if \
            modified_messages[mname][0]["fingerprint"] != \
            modified_messages[mname][1]["fingerprint"] ]
        severity_modified_messages =  { mname : \
            (modified_messages[mname][0]["severity"], \
             modified_messages[mname][1]["severity"]) for mname in \
            list(modified_messages.keys()) if \
            modified_messages[mname][0]["severity"] != \
            modified_messages[mname][1]["severity"] }
        return added_messages, removed_messages, modified_messages, \
            content_modified_messages, severity_modified_messages
    # end _compare_message_dicts

# end class DocDiff

def main():
    if len(sys.argv) != 3:
        print('Usage is python contrail-diff-docs.py <path to old index_logs.doc.schema.json> <path new index_logs.doc.schema.json>')
        exit(-1)
    diff = DocDiff(sys.argv[1], sys.argv[2])
    diff.run()
# end main

if __name__ == "__main__":
    main()
