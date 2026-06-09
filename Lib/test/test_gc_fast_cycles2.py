# Run by test_gc.
from test import support
import _testinternalcapi
import gc
import unittest

class IncrementalGCTests(unittest.TestCase):

    # Use small increments to emulate longer running process in a shorter time
    # @support.gc_threshold(2000, 10)
    def test_incremental_gc_handles_fast_cycle_creation(self):

        class LinkedList:

            #Use slots to reduce number of implicit objects
            __slots__ = "next", "prev", "surprise"

            def __init__(self, next=None, prev=None):
                self.next = next
                self.prev = None
                self.surprise = bytearray(2000)
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

        assert(gc.isenabled())
        g0, g1, g2 = gc.get_threshold();
        print(f"max_young: {g0} yg_loop: {g1} g1_loop: {g2}");

        gc.collect();
        print("gc.collect")
        gids = [];
        for i in range(g1 * g2):
            gids.append(make_ll(g0))

        print("gids prepared")
        # initial_heap_size = _testinternalcapi.get_tracked_heap_size()
        for i in range(2_000):
            # print(f"loop - {i}")
            idx = i % (g1 * g2);
            gids[idx] = make_ll(g0)

        
if __name__ == "__main__":
    unittest.main()
