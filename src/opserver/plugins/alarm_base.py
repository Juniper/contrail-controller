import abc

class AlarmBase(object):
    """Base class for Alarms
    """
    __metaclass__ = abc.ABCMeta

    def __init__(self):
        pass

    @abc.abstractmethod
    def __call__(self, uve_key, uve_data):
        """Evaluate whether alarm should be raised
        :param uve_key: Key of the UVE (a string) 
        :type uve_key: string of the form <Table>:<Key> 
        :param uve_data: UVE Contents - dict of dict
        :param uve_data: dict of dict 
            1st level dict has the UVE Type as key
            2nd level dict has the UVE Attr as key 
        :returns: tuple with alarm name and list of errors as 
            <string rule>,<string contents>
        """
