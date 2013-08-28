#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

def display_user_menu():
    rsp = ''
    rsp += '<hr />'
    rsp += '<a href="/">Home</a> &nbsp;|&nbsp'
    rsp += '<a href="/services">Publishers</a> &nbsp;|&nbsp'
    rsp += '<a href="/clients">Subscribers</a> &nbsp;|&nbsp'
    rsp += '<a href="/stats">Stats</a> &nbsp;|&nbsp'
    rsp += '<a href="/config">Config</a> &nbsp;|&nbsp'
    rsp += '<hr />'
    return rsp
#end display_user_menu
