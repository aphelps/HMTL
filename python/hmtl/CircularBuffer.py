################################################################################
# Author: Adam Phelps
# License: MIT
# Copyright: 2015
#
# Circular data buffer
#
################################################################################

import queue as Queue


class CircularBuffer:
    """
    This class presents a circular buffer that can be used to
    consume and watch data from a stream
    """

    def __init__(self, limit):
        self.limit = limit
        self.queue = Queue.Queue(limit)

    def put(self, elem):
        while True:
            try:
                self.queue.put(elem, False)
                return
            except Queue.Full as e:
                self.queue.get(False)

    def get(self, block=True, wait=3600):
        try:
            return self.queue.get(block, wait)
        except Queue.Empty:
            return None

    def clear(self):
        try:
            while True:
                self.queue.get(False)
        except Queue.Empty:
            pass

    #
    # Iteration
    #
    def __iter__(self):
        return self

    def __next__(self):
        try:
            return self.queue.get(False)
        except Queue.Empty:
            raise StopIteration

    #
    # len()
    #
    def __len__(self):
        return self.queue.qsize()