#include "_rtgc.h"
#include "_rtgc_util.h"
#include <execinfo.h>
#include <stdlib.h>

#define NO_RTGC 0
// static const BOOL FAST_UPDATE_DESTNATION_LINKS = true;
static const BOOL FULL_MANAGED_REF_COUNT = false;

int RTGC_ENABLE = true;
BOOL RTGC_LOG_VERBOSE = true;

RCircuit* _allocateCircuit(void) {
    RCircuit* circuit = malloc(sizeof(RCircuit));
    circuit->_internalRefCount = circuit->ob_refcnt = 0;
    rt_log_v("_allocateCircuit %p\n", circuit);
    return circuit;
}

static const int MAX_CIRCLE_LEN = 4;
RCircuit* _detectCircuit(GCNode* node, GCNode* target) {
#if NO_RTGC
    return NULL;
#else    
    RCircuit* c0 = target->_circuit;
    for (int step = 0; step++ < MAX_CIRCLE_LEN; ) {
        RCircuit* c2 = node->_circuit;
        if (c2 != NULL) {
            if (c0 == NULL) {
                c0 = c2;
            }
            else if (c0 != c2) {
                // 두개의 circuit 을 연결하는 two-way 링크. 
                // (일단 무시. 해당 링크가 해제되지 않으면, 두 circuit 모두 GC 되지 않음)
                return NULL;
            }
        } 
        if (node == target) {
            break;
        }
        if ((node = node->_anchor) == NULL) return NULL;
    }

    rt_log_v("circuit detected %p\n", c0);
    if (c0 == NULL) {
        c0 = _allocateCircuit();
    }
    node = target;
    do {
        if (!node->_circuit) {
            node->_circuit = c0;
            c0->ob_refcnt += node->ob_refcnt;
            /**
             * @brief circuit 생성 시 circuit 내부의 부가적인 연결이 있는 경우, FRC 는 이를 감지하지 못한다.
             * circuit 생성시 circuit 이 단선으로 이뤄진 단순한 원인 경우에 한해서 정상 동작.
             */
            c0->_internalRefCount ++;
        }
        node = node->_anchor;
    } while (node != target);
    return c0;
#endif
}

// static const int EXTERNAL_REF_COUNT_1 = 0x10000;

void __connect_path(GCNode* anchor, GCNode* assigned) {
    assert(anchor != assigned);
#if !NO_RTGC
    assigned->_anchor = anchor;
    if (FULL_MANAGED_REF_COUNT) assigned->ob_refcnt ++;
    RCircuit* c0 = anchor->_circuit;
    RCircuit* circuit = _detectCircuit(anchor, assigned);
    if (circuit != NULL) {
        if (circuit == c0) {
            circuit->_internalRefCount ++;
        }
    }
#endif
}

void __disconnect_path(GCNode* anchor, GCNode* erased) {
    assert(anchor != erased);
#if !NO_RTGC

    if (FULL_MANAGED_REF_COUNT) erased->ob_refcnt --;
    RCircuit* circuit = erased->_circuit;
    if (circuit != NULL) {
        RCircuit* c0 = anchor->_circuit;
        if (circuit == c0) {
            circuit->_internalRefCount --;
        }
    }
    if (erased->_anchor == anchor) { 
        erased->_anchor = NULL;
    }
#endif
}

// ===== //


void RT_onPropertyChanged(PyObject *self, PyObject *erased, PyObject *assigned) {
    if (!RTGC_ENABLE) return;

    printf("RT_onPropertyChanged %p(%s) (%p->%p)\n", self, self->ob_type->tp_name, erased, assigned);
    // if (RTGC_LOG_VERBOSE) printf("RT_onPropertyChanged %p %p -> %p\n", mp, old_value, value);
    if (assigned == erased) return;

    if (erased != NULL && erased != self) {
        __disconnect_path(RT_getGCNode(self), RT_getGCNode(erased));
    }

    if (assigned != NULL && assigned != self) {
        __connect_path(RT_getGCNode(self), RT_getGCNode(assigned));
    }
}


/**
 * FRC 는 key 에 대한 circuit-dectection 을 하지 않는다.
 */
void RT_onDictEntryRemoved_obsolete(PyObject *mp, PyObject *key, PyObject *value) {
    // key may be null! 
    // if (RTGC_LOG_VERBOSE) printf("RT_onDictEntryRemoved %p[%p] = %p\n", mp, key, value);
}

void RT_replaceReferrer(PyObject *obj, PyObject *old_anchor, PyObject *new_anchor) {
#if !NO_RTGC
    if (!RTGC_ENABLE) return;
    if (RTGC_LOG_VERBOSE) printf("RT_replaceReferrer %p (%p => %p)\n", obj, old_anchor, new_anchor);
    if (old_anchor != NULL && old_anchor == RT_getGCNode(obj)->_anchor) {
        __disconnect_path(old_anchor, obj);
    }
    if (new_anchor != NULL && new_anchor != obj) {
        __connect_path(new_anchor, obj);
    }
#endif
}

void RT_onIncreaseRefCount(PyObject *obj) {
#if !NO_RTGC
    rt_assert(obj != NULL);

    if (RTGC_LOG_VERBOSE) {
        if (RT_getGCNode(obj)->_circuit != NULL) {
            printf("RT_onIncreaseRefCount %p(%s) (c=%p)\n", obj, obj->ob_type->tp_name, RT_getGCNode(obj)->_circuit);
        }
    }
    if (!RTGC_ENABLE) return;
    RCircuit* circuit = RT_getGCNode(obj)->_circuit;
    if (circuit != NULL) {
        // stack-ref 와 참조-ref 를 구별할 수 없다.
        circuit->ob_refcnt ++;
    }
    // if (RTGC_LOG_VERBOSE) printf("RT_onIncreaseRefCount %p\n", obj);
    // (3UL << 15) = Py_TPFLAGS_HAVE_STACKLESS_EXTENSION 으로 예약됨. RTGC Flag 로 사용
    rt_assert((Py_TYPE(obj)->tp_flags & (3UL << 15)) == 0);
#endif
}

BOOL RT_onDecreaseRefCount(PyObject *obj) {
#if !NO_RTGC
    rt_assert(obj != NULL);

    if (RTGC_LOG_VERBOSE) {
        if (RT_getGCNode(obj)->_circuit != NULL) {
            printf("RT_onDecreaseRefCount %p(%s) (c=%p)\n", obj, obj->ob_type->tp_name, RT_getGCNode(obj)->_circuit);
        }
    }
    if (!RTGC_ENABLE) return true;
    RCircuit* circuit = RT_getGCNode(obj)->_circuit;
    if (circuit != NULL) {
        // stack-ref 와 참조-ref 를 구별할 수 없다.
        if (--circuit->ob_refcnt == circuit->_internalRefCount) {
            if (RTGC_LOG_VERBOSE) {
                printf("Garbage circuit detected %p(%s) (c=%p)\n", obj, obj->ob_type->tp_name, RT_getGCNode(obj)->_circuit);
            }
            return false;
        }
    }
    // if (RTGC_LOG_VERBOSE) printf("RT_onDecreaseRefCount %p\n", obj);
    // (3UL << 15) = Py_TPFLAGS_HAVE_STACKLESS_EXTENSION 으로 예약됨. RTGC Flag 로 사용
    // rt_assert((Py_TYPE(obj)->tp_flags & (3UL << 15)) == 0);
#endif
    return true;
}

static int deassignGarbageAnchor(PyObject* obj, void* anchor) {
    // printf("-- deassignGarbageAnchor %p\n", anchor);
    RT_onPropertyChanged((PyObject*)anchor, obj, NULL);
    return 0;
} 

void RT_onDestoryGarbageNode(PyObject *obj, PyTypeObject *type) {
#if !NO_RTGC
    // if (!RTGC_ENABLE) return;

    // printf("RT_onDestoryGarbageNode %p %s\n", obj, type == NULL ? NULL : type->tp_name);
    traverseproc traverse = type->tp_traverse;
    if (traverse != NULL) {
        traverse(obj, deassignGarbageAnchor, obj);
    }
#endif
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
    cnt_break ++;
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
