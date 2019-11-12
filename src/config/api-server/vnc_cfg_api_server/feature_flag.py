#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
"""
Feature flag initializer.
"""

import yaml


FEATURE_MATADATE_FILE = "../feature_metadata.yaml"


class VncFeatureFlag(object):
    """
    This is the feature flag class to initialize configurable features.
    """

    def __init__(self):
        self.configurable_features = yaml.load(open(FEATURE_MATADATE_FILE))

        self.get_present_features()
        self.compare_features()


    def get_present_features(self):
        # Read db
        self.dbe_features = []

    def compare_features(self):
        # Update the list of introduced, retired, evolved features
        self.introduced = []
        self.retired = []
        self.evolved = []
