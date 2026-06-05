# Run by test_gc.
from test import support
import _testinternalcapi
import gc
import unittest

class IncrementalGCTests(unittest.TestCase):

    # Use small increments to emulate longer running process in a shorter time
    @support.gc_threshold(2000, 10)
    def test_incremental_gc_handles_fast_cycle_creation(self):

        class LinkedList:

            #Use slots to reduce number of implicit objects
            __slots__ = "next", "prev", "surprise"

            def __init__(self, next=None, prev=None):
                self.next = next
                self.prev = None
                # if next is not None:
                #     next.prev = self
                # self.prev = prev
                # if prev is not None:
                #     prev.next = self

        def make_ll(depth):
            head = LinkedList()
            for i in range(depth):
                head = LinkedList(head, head.prev)
            head.next.prev = head
            return head

        head = make_ll(1000*1000)

        assert(gc.isenabled())
        olds = []
        initial_heap_size = _testinternalcapi.get_tracked_heap_size()
        idx = 0;
        for i in range(20_000):
            newhead = make_ll(300)
            newhead.surprise = head
            idx += 1;
            if len(olds) == 200:
                new_objects = _testinternalcapi.get_tracked_heap_size() - initial_heap_size
                # self.assertLess(new_objects, 27_000, f"Heap growing. Reached limit after {i} iterations")
                olds[idx % 200] = newhead;
            else:
                olds.append(newhead)

        
if __name__ == "__main__":
    unittest.main()
