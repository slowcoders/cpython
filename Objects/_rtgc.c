#include "_rtgc.h"
#include "_rtgc_util.h"
#include <execinfo.h>
#include <stdlib.h>

static const BOOL true = 1;
static const BOOL false = 0;
static const BOOL FAST_UPDATE_DESTNATION_LINKS = true;
static const CircuitNode* NoCircuit = (CircuitNode*)-1;

static CircuitNode* TR_replaceDestinationLinksOfIncomingPath(GCNode* node, LinkArray* addedLinks, LinkArray* erasedLinks);



static void _initTempLinks(LinkArray* tmpArray, GCNode* endpoint) {
    ContractedLink* link = (ContractedLink*)(tmpArray + 1);
    link->_endpoint = (GCNode*)endpoint;
    link->_linkCount = 1;
    tmpArray->_items = link;
    tmpArray->_size = 1;
    tmpArray->_owner = NULL;
}

static LinkArray* _getDestinationLinks(GCNode* node, LinkArray* tmpArray) {
    if (node == NULL || node->_nodeType == Acyclic) {
        return NULL;
    }
    if (node->_nodeType == Transit) {
        return ((GCNode*)node)->destinations;
    }
    else {
        assert(node->_nodeType == Endpoint);
        _initTempLinks(tmpArray, (GCNode*)node);
        return tmpArray;
    }
}

void TR_increaseGroundRefCount(GCNode* node) {
    // TR_checkType(self);
    while (node->_groundRefCount++ == 0) {
        FOR_EACH_CONTRACTED_LINK(node->destinations) {
            TR_increaseGroundRefCount(iter._link->_endpoint);
        }
    }
}

void TR_decreaseGroundRefCount(GCNode* node) {
    // TR_checkType(self);
    while (--node->_groundRefCount == 0) {
        FOR_EACH_CONTRACTED_LINK(node->destinations) {
            TR_decreaseGroundRefCount(iter._link->_endpoint);
        }
    }
}

void _addDestinationToIncomingPaths(GCNode* self, GCNode* dest) {
    int dest_level = dest->_level;
    if (self->_level < dest_level) {
        Cll_push(self->_destinations, dest);
        FOR_EACH_CONTRACTED_LINK(self->_anchors) {
            assert(iter._link->_endpoint->_level >= self->_level);
            _addDestinationToIncomingPaths(iter._link->_endpoint, dest);
        }
    } else {
        Cll_push(dest->_anchors, self);
    }
}

void _addAnchorToOutgoingPaths(GCNode* self, GCNode* anchor) {
    if (self->_level <= anchor->_level) {
        Cll_push(self->_anchors, anchor);
        FOR_EACH_CONTRACTED_LINK(self->_destinations) {
            _addAnchorToOutgoingPaths(iter._link->_endpoint, anchor);
        }
    } else {
        Cll_push(anchor->_destinations, self);
    }
}

void TR_addIncomingLink(GCNode* self, GCNode* anchor) {
    if (anchor->_level <= self->_level) {
        _addDestinationToIncomingPaths(anchor, self);
    } else {
        _addAnchorToOutgoingPaths(self, anchor);
    }
}

void _removeDestinationFromIncomingPaths(GCNode* self, GCNode* dest) {
    int dest_level = dest->_level;
    if (self->_level < dest_level) {
        Cll_removeFast(self->_destinations, dest);
        FOR_EACH_CONTRACTED_LINK(self->_anchors) {
            assert(iter._link->_endpoint->_level >= self->_level);
            _removeDestinationFromIncomingPaths(iter._link->_endpoint, dest);
        }
    } else {
        Cll_removeFast(dest->_anchors, self);
    }
}

void _removeAnchorFromOutgoingPaths(GCNode* self, GCNode* anchor) {
    if (self->_level <= anchor->_level) {
        Cll_removeFast(self->_anchors, anchor);
        FOR_EACH_CONTRACTED_LINK(self->_destinations) {
            _removeAnchorFromOutgoingPaths(iter._link->_endpoint, anchor);
        }
    } else {
        Cll_removeFast(anchor->_destinations, self);
    }
}

void TR_removeIncomingLink(GCNode* self, GCNode* anchor) {
    if (anchor->_level <= self->_level) {
        _removeDestinationFromIncomingPaths(anchor, self);
    } else {
        _removeAnchorFromOutgoingPaths(self, anchor);
    }
}

void RT_onReferentChanged(GCNode* self, GCObject* erased, GCObject* assigned) {
    if (erased != NULL) {
        TR_removeIncomingLink(erased, self);
    }
    if (assigned != NULL) {
        TR_addIncomingLink(assigned, self);
    }    
}


/*
가비지 객체에 의한 참조 삭제.
*/

BOOL TR_isGarbage(GCNode* self) {
    TR_checkType(self);
    return self->_groundRefCount == 0 && Cll_Size(self->_anchors) == 0;
}

// ===== //

void EP_increaseGroundRefCount(GCNode* node, int count) {
    assert(count > 0);
    if (node->_groundRefCount == 0) {
        FOR_EACH_CONTRACTED_LINK(node->_destinations) {
            EP_increaseGroundRefCount(iter._link->_endpoint, 1);
        }
    }
    node->_groundRefCount += count;
}

void EP_decreaseGroundRefCount(GCNode* node, int count) {
    assert(count > 0);
    if ((node->_groundRefCount -= count) == 0) {
        FOR_EACH_CONTRACTED_LINK(node->_destinations) {
            EP_decreaseGroundRefCount(iter._link->_endpoint, 1);
        }
    }
}



void RT_collectGarbage(GCNode* node, void* dealloc) {
    // assert(RT_isGarbage(node));
    auto obj = RT_getObject(node);
    RT_markDestroyed(node);
    // std::vector<GCObject*> referents;
    // obj->getReferents(obj, referents);
    // for (auto referent : referents) {
    //     auto node = referent->_node;
    //     if (node->isDestroyed()) continue;
    //     node->removeGarbageReferrer(obj);
    //     if (node->isGarbage()) {
    //         collectGarbage(node);
    //     }
    // }
}

int RTGC_ENABLE = true;
BOOL RTGC_DEBUG_VERBOSE = true;

void RT_onPropertyChanged(PyObject *mp, PyObject *old_value, PyObject *value) {
    // if (RTGC_DEBUG_VERBOSE) printf("RT_onPropertyChanged %p %p -> %p\n", mp, old_value, value);
}

void RT_onDictEntryInserted(PyObject *mp, PyObject *key, PyObject *value) {
    // if (RTGC_DEBUG_VERBOSE) printf("RT_onDictEntryInserted %p[%p] = %p\n", mp, key, value);
}

void RT_onDictEntryRemoved(PyObject *mp, PyObject *key, PyObject *value) {
    // key may be null! 
    // if (RTGC_DEBUG_VERBOSE) printf("RT_onDictEntryRemoved %p[%p] = %p\n", mp, key, value);
}

void RT_replaceReferrer(PyObject *obj, PyObject *old_referrer, PyObject *referrer) {
    // if (RTGC_DEBUG_VERBOSE) printf("RT_replaceReferrer %p (%p => %p)\n", obj, old_referrer, referrer);
}

void RT_increaseGRefCount(PyObject *obj) {
    rt_assert(obj != NULL);
    if (RTGC_ENABLE) EP_increaseGroundRefCount(RT_getGCNode(obj), 1);
    // if (RTGC_DEBUG_VERBOSE) printf("RT_increaseGRefCount %p\n", obj);
    // (3UL << 15) = Py_TPFLAGS_HAVE_STACKLESS_EXTENSION 으로 예약됨. RTGC Flag 로 사용
    rt_assert((Py_TYPE(obj)->tp_flags & (3UL << 15)) == 0);
}

void RT_decreaseGRefCount(PyObject *obj) {
    rt_assert(obj != NULL);
    if (RTGC_ENABLE) EP_decreaseGroundRefCount(RT_getGCNode(obj), 1);
    // if (RTGC_DEBUG_VERBOSE) printf("RT_decreaseGRefCount %p\n", obj);
    // (3UL << 15) = Py_TPFLAGS_HAVE_STACKLESS_EXTENSION 으로 예약됨. RTGC Flag 로 사용
    rt_assert((Py_TYPE(obj)->tp_flags & (3UL << 15)) == 0);
}

static int exit_on_break = true;
void
RT_break (void)
{
  void *array[50];
  char **strings;
  int size, i;

  size = backtrace (array, 50);
  strings = backtrace_symbols (array, size);
  if (strings != NULL)
  {

    printf ("Obtained %d stack frames.\n", size);
    for (i = 0; i < size; i++)
      printf ("%s\n", strings[i]);
  }
  free (strings);
  assert(!exit_on_break);
}

static int cnt_break = 0;
Py_NO_INLINE PyAPI_FUNC(void) break_rt(int stop) {
    if (stop) {
        RT_break();
        // PyErr_BadInternalCall();
    }
}

#ifdef Py_REF_DEBUG
#else
void
_Py_NegativeRefcount(const char *filename, int lineno, PyObject *op)
{
    _PyObject_AssertFailed(op, NULL, "object has negative ref count",
                           filename, lineno, __func__);
}
Py_ssize_t _Py_RefTotal;
#endif