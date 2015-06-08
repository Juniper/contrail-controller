#!/usr/bin/python

from gevent import monkey
monkey.patch_all()
import logging
import gevent
from gevent.coros import BoundedSemaphore
from kafka import KafkaClient, KeyedProducer, SimpleConsumer, common
from uveserver import UVEServer
import os
import json
import copy
import traceback

class PartitionHandler(gevent.Greenlet):
    def __init__(self, brokers, partition, group, topic, logger, limit):
        gevent.Greenlet.__init__(self)
        self._brokers = brokers
        self._partition = partition
        self._group = group
        self._topic = topic
        self._logger = logger
        self._limit = limit
        self._partdb = {}
        self._partoffset = None
        self._kfk = None

    def msg_handler(self, om):
        self._partoffset = om.offset
        self._partdb[om.message.key] = om.message.value 
        self._logger.info("%d Reading %s" % (self._partition, str(om)))
        return True

    def start_partition(self):
        self._logger.info("%d Starting DB" % self._partition)
        return True

    def stop_partition(self):
        self._logger.info("%d Stopping DB" % self._partition)
        return True

    def _run(self):
	pcount = 0
        while True:
            try:
                self._logger.info("New KafkaClient %d" % self._partition)
                self._kfk = KafkaClient(self._brokers ,str(os.getpid()))
                try:
                    consumer = SimpleConsumer(self._kfk, self._group, self._topic, buffer_size = 4096*4, max_buffer_size=4096*32)
                    #except:
                except Exception as ex:
                    template = "Consumer Failure {0} occured. Arguments:\n{1!r}"
                    messag = template.format(type(ex).__name__, ex.args)
                    self._logger.info("%s" % messag)
                    raise RuntimeError(messag)

                self._logger.info("Starting %d" % self._partition)

                # Find the offset of the last message that has been queued
                consumer.seek(0,2)
                try:
                    mi = consumer.get_message(timeout=0.1)
                    consumer.commit()
                except common.OffsetOutOfRangeError:
                    mi = None
                #import pdb; pdb.set_trace()
                self._logger.info("Last Queued for %d is %s" % \
                                  (self._partition,str(mi)))
                self.start_partition()

                # start reading from last previously processed message
                consumer.seek(0,1)

                if self._limit:
                    raise gevent.GreenletExit

                while True:
                    try:
                        mlist = consumer.get_messages(10)
                        for mm in mlist:
                            if mm is None:
                                continue
                            self._logger.debug("%d Reading offset %d" % \
                                    (self._partition, mm.offset))
                            consumer.commit()
                            pcount += 1
                            if not self.msg_handler(mm):
                                self._logger.info("%d could not handle %s" % (self._partition, str(mm)))
                                raise gevent.GreenletExit
                    except TypeError:
                        gevent.sleep(0.1)
                    except common.FailedPayloadsError as ex:
                        self._logger.info("Payload Error: %s" %  str(ex.args))
                        gevent.sleep(0.1)
            except gevent.GreenletExit:
                break
            except Exception as ex:
                template = "An exception of type {0} occured. Arguments:\n{1!r}"
                messag = template.format(type(ex).__name__, ex.args)
                self._logger.info("%s : traceback %s" % \
                                  (messag, traceback.format_exc()))
                self.stop_partition()
                gevent.sleep(2)
        self._logger.info("Stopping %d pcount %d" % (self._partition, pcount))
        return self._partoffset, self._partdb

class UveStreamProc(PartitionHandler):
    
    # Arguments:
    #
    #  brokers   : broker list for kafka bootstrap
    #  partition : partition number
    #  uve_topic : topic to subscribe to
    #  logger    : logging object to use  
    #  uvecb     : Callback to get the 
    #            : initial UVE DB (map of collector info,
    #              leading to map of generator info,
    #              which leads to set of UVE Keys
    #  callback  : Callback function for reporting the set of the UVEs
    #              that may have changed for a given notification
    def __init__(self, brokers, partition, uve_topic, logger, uvecb, callback):
        super(UveStreamProc, self).__init__(brokers, partition, "workers", uve_topic, logger, False)
        self._uvedb = {}
        self._uvein = {}
        self._uveout = {}
        self._uvecb = uvecb
        self._callback = callback
        self._partno = partition

    def __del__(self):
        self._logger.info("Destroying UVEStream for part %d" % self._partno)

    def start_partition(self):
        ''' This function loads the initial UVE database.
            for the partition
        '''
        self._uvedb = self._uvecb(self._partno)
        self._logger.debug("Starting part %d with UVE db %s" % \
                           (self._partno,str(self._uvedb)))
        uves  = {}
        for kcoll,coll in self._uvedb.iteritems():
            for kgen,gen in coll.iteritems():
                for kk in gen.keys():
                    uves[kk] = None
        self._logger.info("Starting part %d with UVE keys %s" % \
                          (self._partno,str(uves)))
        self._callback(self._partno, uves)

    def contents(self):
        return self._uvedb

    def stats(self):
        ''' Return the UVEKey-Count stats collected over 
            the last time period for this partition, and 
            the incoming UVE Notifs as well.
            Also, the stats should be cleared to prepare
            for the next period of collection.
        '''
        ret_out = copy.deepcopy(self._uveout)
        ret_in  = copy.deepcopy(self._uvein)
        self._uveout = {}
        self._uvein = {}
        return ret_in, ret_out
    
    def msg_handler(self, om):
        self._partoffset = om.offset
        chg = {}
        try:
            uv = json.loads(om.message.value)
            self._partdb[om.message.key] = uv
            self._logger.debug("%d Reading UVE %s" % (self._partition, str(om)))
            gen = uv["gen"]
            coll = uv["coll"]

            if not self._uvedb.has_key(coll):
                self._uvedb[coll] = {}
            if not self._uvedb[coll].has_key(gen):
                self._uvedb[coll][gen] = {}

            if (uv["message"] == "UVEUpdate"):
                if self._uvedb[coll][gen].has_key(uv["key"]):
                    self._uvedb[coll][gen][uv["key"]] += 1
                else:
                    self._uvedb[coll][gen][uv["key"]] = 1

                tab = uv["key"].split(":")[0]

                # Record stats on UVE Keys being processed
                if not self._uveout.has_key(tab):
                    self._uveout[tab] = {}
                if self._uveout[tab].has_key(uv["key"]):
                    self._uveout[tab][uv["key"]] += 1
                else:
                    self._uveout[tab][uv["key"]] = 1

                # Record stats on the input UVE Notifications
                if not self._uvein.has_key(tab):
                    self._uvein[tab] = {}
                if not self._uvein[tab].has_key(coll):
                    self._uvein[tab][coll] = {}
                if not self._uvein[tab][coll].has_key(gen):
                    self._uvein[tab][coll][gen] = {}
                if not self._uvein[tab][coll][gen].has_key(uv["type"]):
                    self._uvein[tab][coll][gen][uv["type"]] = 1
                else:
                    self._uvein[tab][coll][gen][uv["type"]] += 1

                chg[uv["key"]] = set()
                chg[uv["key"]].add(uv["type"])
            else:
                # Record stats on UVE Keys being processed
                for uk in self._uvedb[coll][gen].keys():
                    tab = uk.split(":")[0]
                    if not self._uveout.has_key(tab):
                        self._uveout[tab] = {}

                    if self._uveout[tab].has_key(uk):
                        self._uveout[tab][uk] += 1
                    else:
                        self._uveout[tab][uk] = 1
                
                    # when a generator is delelted, we need to 
                    # notify for *ALL* its UVEs
                    chg[uk] = None

                del self._uvedb[coll][gen]

                # TODO : For the collector's generator, notify all
                #        UVEs of all generators of the collector
        except Exception as ex:
            template = "An exception of type {0} in uve proc . Arguments:\n{1!r}"
            messag = template.format(type(ex).__name__, ex.args)
            self._logger.info("%s" % messag)
            return False
        else:
            self._callback(self._partno, chg)
        return True
           
if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO,
        format='%(asctime)s %(levelname)s %(message)s')

    workers = {}
    brokers = "localhost:9092,localhost:9093,localhost:9094"
    group = "workers"
    
    kafka = KafkaClient(brokers,str(os.getpid()))
    cons = SimpleConsumer(kafka, group, "ctrl")
    cons.provide_partition_info()
    print "Starting control"
    end_ready = False
    while end_ready == False:
	try:
	    while True:
		part, mmm = cons.get_message(timeout=None)
                mm = mmm.message
		print "Consumed ctrl " + str(mm)
                if mm.value == "start":
                    if workers.has_key(mm.key):
                        print "Dup partition %s" % mm.key
                        raise ValueError
                    else:
                        ph = UveStreamProc(brokers, int(mm.key), "uve-" + mm.key, "alarm-x" + mm.key, logging)
                        ph.start()
                        workers[int(mm.key)] = ph
                elif mm.value == "stop":
                    #import pdb; pdb.set_trace()
                    if workers.has_key(int(mm.key)):
                        ph = workers[int(mm.key)]
                        gevent.kill(ph)
                        res,db = ph.get()
                        print "Returned " + str(res)
                        print "State :"
                        for k,v in db.iteritems():
                            print "%s -> %s" % (k,str(v)) 
                        del workers[int(mm.key)]
                else:
                    end_ready = True
                    cons.commit()
		    gevent.sleep(2)
                    break
	except TypeError:
	    gevent.sleep(0.1)
	except common.FailedPayloadsError as ex:
	    print "Payload Error: " + str(ex.args)
	    gevent.sleep(0.1)
    lw=[]
    for key, value in workers.iteritems():
        gevent.kill(value)
        lw.append(value)

    gevent.joinall(lw)
    print "Ending Consumers"

