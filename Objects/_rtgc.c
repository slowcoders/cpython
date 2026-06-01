#include "_rtgc.h"
#include "_rtgc_util.h"
#include "pycore_gc.h"
#include <execinfo.h>
#include <stdlib.h>

#define NO_RTGC 1
// static const BOOL FAST_UPDATE_DESTNATION_LINKS = true;
// static const BOOL FULL_MANAGED_REF_COUNT = false;
// static const int MAX_CIRCLE_LEN = 4;

int RTGC_ENABLE = true;
BOOL RTGC_LOG_VERBOSE = true;

RCircuit* _allocateCircuit(void) {
    RCircuit* circuit = malloc(sizeof(RCircuit));
    circuit->_internalRefCount = circuit->ob_refcnt = 0;
    rt_log_v("_allocateCircuit %p\n", circuit);
    return circuit;
}

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


#if 0

static const int MAX_REF_COUNT_IN_STACK_CHUNK = 4090;
typedef struct _RtgcStackChunk {
    RtgcStack* prev;
    RtgcStack* next;
    int count;
    PyObject* refs[MAX_REF_COUNT_IN_STACK_CHUNK];
} RtgcStackChunk;

RtgcStack g_refStack = {
    .prev = NULL,
    .next = NULL,
    count = 0,
};

static RtgcStack* topStack = &g_refStack;

static inline void
ref_stack_push(PyObject* po)
{
    assert(topStack->count < MAX_REF_COUNT_IN_STACK_CHUNK);
    if (topStack->count == MAX_REF_COUNT_IN_STACK_CHUNK - 1) {
        RtgcStack* next = topStack->next;
        if (next == NULL) {
            next = (RtgcStack*)malloc(sizeof(RtgcStack));
            next->prev = topStack;
            next->next = NULL;
            next->count = 0;
            topStack->next = next;
        } else {
            assert(next->count == 0);
            assert(next->prev == topStack);
        }
        topStack = next;
    }
    topStack->refs[topStack->count] = po;
    topStack->count ++;
}


static inline PyObject*
ref_stack_pop()
{
    if (topStack->count == 0) {
        if (topStack == &g_refStack) {
            return NULL;
        }
        topStack = topStack->prev;
        assert(topStack != NULL);
        assert(topStack->count == MAX_REF_COUNT_IN_STACK_CHUNK);
    }
    PyObject* po = topStack->refs[--topStack->count];
    return po;
}


static void
mark_reachable(PyObject* po, void* parent) {
    if (_PyObject_IS_GC(op)) {
        PyGC_Head *gc = AS_GC(op);
        if (!gc_is_collecting(gc)) continue;

        if (!rtgc_is_marked(gc)) {
            GC_SET_PREV(gc) = 
            ref_stack_push(op);
        }
        else if (rtgc_is_scanning(op)) {
            // cycle.
        }
    }
}

static inline void
drain_ref_stack() {
    for (PyObject* po; (po = ref_stack_pop()) != NULL; ) {
        PyObject *op = FROM_GC(gc);
        traverse = Py_TYPE(op)->tp_traverse;

        (void) traverse(op,
                        mark_reachable,
                        containers);
    }
}
#endif

#if 0 // def ENABLE_RTGC

#define RTGC_MARK_SCANNING 1
#define RTGC_MARK_UNSAFE    1
#define RTGC_GC_DEBUG 1

static void
rtgc_collect_unreachable(PyGC_Head *unsafe, 
                         PyGC_Head *unreachable,
                         GCState *gcstate);



static inline bool
rtgc_is_unsafe(PyObject *op)
{
    return (op->ob_overflow & RTGC_MARK_UNSAFE) != 0;
}


static inline void
rtgc_unmark_unsafe(PyObject *op)
{
    if (rtgc_is_unsafe(op)) {
        op->ob_overflow &= ~RTGC_MARK_UNSAFE;
    }
}

static inline bool
rtgc_mark_scanning(PyObject *op, PyGC_Head* scanning_list)
{    
    assert(!rtgc_is_unsafe(op));

    if (op->ob_overflow == MIN_RTGC_STABLE_REF_COUNT) return true;
    if ((op->ob_flags & RTGC_MARK_SCANNING) != 0) return false;

    op->ob_flags |= RTGC_MARK_SCANNING;

    PyGC_Head* gc = AS_GC(op);
    gc_list_append(gc, scanning_list);

    return true;
}


static const int GC_PREV_INCREMENT = (1 << _PyGC_PREV_SHIFT);
static int
visit_detect_cycle(PyObject *op, void* referents) {
    rtgc_unmark_unsafe(op);
    PyGC_Head* gc = AS_GC(op);
    if ((op->ob_flags & RTGC_MARK_SCANNING) != 0) {
        gc->_gc_prev -= GC_PREV_INCREMENT;
        if (gc->_gc_prev < GC_PREV_INCREMENT) {
            // cyclic garbage detect!!!
        }
        return 0;
    }

    if (op->ob_refcnt > 1) {
        gc_list_append(gc, NULL/*scanning_list*/);
        return 0;
    }

    if (rtgc_mark_scanning(op, NULL)) {
        PyGC_Head* gc = AS_GC(op);
        _PyGCHead_SET_PREV(gc, referents);
        _PyGCHead_SET_NEXT(referents, gc);
    }
    return 0;
}

bool
rtgc_scan_unsafe_links(PyObject *op, RtgcState *rtgcstate) {
    rtgc_unmark_unsafe(op);

    rtgc_mark_scanning(op, NULL);

    traverseproc traverse = Py_TYPE(op)->tp_traverse;
    (void) traverse(op,
                    visit_detect_cycle,
                    rtgcstate);
}

void
rtgc_collect_unreachable(PyGC_Head *containers, PyGC_Head *unreachable, GCState *gcstate) {
    PyGC_Head *next;
    PyGC_Head *gc = GC_NEXT(containers);

    RtgcState rtgcstate;
    rtgcstate.unsafe_list = &gcstate->young.head;
    rtgcstate.pending_list = &gcstate->old[gcstate->visited_space^1].head;
    rtgcstate.visited_list = &gcstate->old[gcstate->visited_space].head;

    for (; gc != containers; gc = next) {
        next = GC_NEXT(gc);
        PyObject *op = FROM_GC(gc);
        assert(!_Py_IsImmortal(op));
        if (rtgc_is_unsafe(op)) {
            rtgc_scan_unsafe_links(op, &rtgcstate);
        }
    }
}


void gc_collect_rtgc(PyThreadState *tstate,
                 struct gc_collection_stats *stats) 
{
    GC_STAT_ADD(2, collections, 1);
    GCState *gcstate = &tstate->interp->gc;
    validate_spaces(gcstate);
    PyGC_Head *unsafe = &gcstate->young.head;
    // PyGC_Head *pending = &gcstate->old[gcstate->visited_space^1].head;
    // PyGC_Head *visited = &gcstate->old[gcstate->visited_space].head;
    untrack_tuples(unsafe);
    /* merge all generations into visited */
    // gc_list_merge(young, pending);
    // gc_list_validate_space(pending, 1-gcstate->visited_space);
    // gc_list_set_space(pending, gcstate->visited_space);
    gcstate->young.count = 0;
    // PyGC_Head *unsafe_first = unsafe->_gc_next;
    gc_list_init(unsafe);

    // gc_list_merge(pending, visited);
    validate_spaces(gcstate);

    PyGC_Head survivors;
    gc_list_init(&survivors);
    // gc_list_set_space(unsafe, gcstate->visited_space);
    gc_collect_region(tstate, unsafe, &survivors, stats);

    validate_spaces(gcstate);
    gcstate->young.count = 0;
    gcstate->old[0].count = 0;
    gcstate->old[1].count = 0;

    // ?? completed_scavenge(gcstate);
    _PyGC_ClearAllFreeLists(tstate->interp);
    validate_spaces(gcstate);
    add_stats(gcstate, 2, stats);
}

#endif


volatile uint g_cntTrace = 0;
volatile uint g_dbgTrace = INT_MAX;
volatile char* g_dbgType = NULL;
volatile PyObject* g_dbgObj = NULL;

int cnt_dump = 0;
PyAPI_FUNC(void) RTGC_dump(PyObject* op, const char* tag) {
    printf("[%d:%d] %s %s %p, %d/%d (%d)\n", cnt_dump, g_cntTrace, tag, op->ob_type->tp_name, op, op->ob_overflow/MIN_RTGC_STABLE_REF_COUNT, op->ob_refcnt, PyType_IS_GC(Py_TYPE(op)));
    if (++cnt_dump > 1000) {
        exit(-1);
    }
}

PyAPI_FUNC(void) RTGC_trace(PyObject* op, const char* tag) {
    // if (!strcmp(op->ob_type->tp_name, "str")) {
    //     RTGC_dump(op, tag);
    // }
    g_cntTrace++;
    if (g_cntTrace > g_dbgTrace) {
        // if (op != g_dbgObj && ((int64_t)op & 0xFFF) != 0x1c0) {
        //     return;
        // }
        if (g_dbgType != NULL && 0 != strcmp(op->ob_type->tp_name, g_dbgType)) {
            return;
        }
    
        RTGC_dump(op, tag);
    }
}


