import unittest

from multicloud import Multicloud


class TestMulticloud(unittest.TestCase):
    def test_creation(self):
        Multicloud(None)

    def test_logger(self):
        class SampleLogger():
            pass
        logger = SampleLogger()

        mc = Multicloud(logger)

        self.assertIsInstance(mc.logger, SampleLogger)
