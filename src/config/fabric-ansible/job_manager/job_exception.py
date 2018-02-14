#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
Job manager exception class
"""


class JobException(Exception):

    def __init__(self, *args, **kwargs):
        Exception.__init__(self)
