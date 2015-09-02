#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""Utilities and helper functions."""

import gevent
import hashlib


def publisher_id(remote, infostr):
    return hashlib.md5(remote + infostr).hexdigest()
# end publisher_id


def do_html_url(url, name):
    return '<a href="%s">' % (url) + name + '</a>'
