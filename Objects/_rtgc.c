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
        return ((GCNode*)node)->_destinations;
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
        FOR_EACH_CONTRACTED_LINK(node->_destinations) {
            // 중복 처리 검사 필요!!!!!
            TR_increaseGroundRefCount(iter._link->_endpoint);
        }
    }
}

void TR_decreaseGroundRefCount(GCNode* node) {
    // TR_checkType(self);
    while (--node->_groundRefCount == 0) {
        FOR_EACH_CONTRACTED_LINK(node->_destinations) {
            // 중복 처리 검사 필요!!!!!
            TR_decreaseGroundRefCount(iter._link->_endpoint);
        }
    }
}

typedef void (*UpdateAnchorConnection_FN)(GCNode* anchor, GCNode* node, int count);
typedef BOOL (*UpdateDestinationConnections_FN)(GCNode* node, GCNode* target, LinkArray* destinations);
typedef void (*OnFoundSourceNode_FN)(GCNode* anchor, TrackContext* context);

typedef struct {
    GCNode* _node; 
    UpdateAnchorConnection_FN _updateAnchorConnection;
    UpdateDestinationConnections_FN _updateDestinationConnections;
    BOOL _inProgressLevelUp;
    OnFoundSourceNode_FN _onFoundSourceNode;
    int _cntMarked;
    int _cntNewAnchors;
    GCNode* _marked[2048];
} TrackContext;

BOOL _isMarked(GCNode* node) { 
    return false; 
}
void _addMarked(TrackContext* ctx, GCNode* node) { 
    assert(!_isMarked(node));
    ctx->_marked[ctx->_cntMarked ++] = node;    
}

void _clearMarked(TrackContext* ctx) { 
    GCNode** pMarked = ctx->_marked;
    for (int i = ctx->_cntMarked; --i >= 0; ) {
        GCNode* node = *pMarked++;
        assert(_isMarked(node));
    }
}


void _addDestinationToIncomingPaths(GCNode* self, int count, TrackContext* ctx) {
    _addMarked(ctx, self);
    GCNode* dest = ctx->_node;    
    if (self->_level <= dest->_level) {
        if (self == dest) {
            // 순환 참조!!!
            if (ctx->_inProgressLevelUp) {
                dest->_level ++;
            } else {
                // clearMark 필요!!!
                _levelUpNode(self, dest->_level + 1);
            }
        }
        Cll_add(self->_destinations, dest, count);
        FOR_EACH_CONTRACTED_LINK(self->_anchors) {
            assert(iter._link->_endpoint->_level >= self->_level);
            if (!_isMarked(iter._link->_endpoint)) {
                _addDestinationToIncomingPaths(iter._link->_endpoint, iter._link->_linkCount, ctx);
            }
        }
    } else {
        Cll_add(dest->_anchors, self, count);
    }
}

void _addAnchorToOutgoingPaths(GCNode* self, int count, TrackContext* ctx) {
    GCNode* new_anchor = ctx->_node;    
    assert (new_anchor->_level >= self->_level);

    _addMarked(ctx, self);
    Cll_add(self->_anchors, new_anchor, count);
    FOR_EACH_CONTRACTED_LINK(self->_destinations) {
        GCNode* dest = iter._link->_endpoint;
        if (new_anchor->_level > dest->_level) {
            _addAnchorToOutgoingPaths(dest, count, ctx);
        } else if (new_anchor->_level == dest->_level) {
            // dest->_anchors 중 new_anchor 를 경유하는 anchor 를 new_anchor->_anchor 로 옮겨야 한다.
            ;;
            // dest 의 destinations 모두를 new_anchor->_destination 에 추가하여야 한다. 
            ;; //_addDestinationToIncomingPaths(dest, count, ctx);
        } else {
            // ctx 초기화 필요.
            _addDestinationToIncomingPaths(new_anchor, dest, ctx);
        }
    }
}

typedef struct {
    _Item* _linkItems;
    int _count;
    int _maxLevel;
} RemoveAnchorSet;


void _replaceAnchorSetToNewAnchor(GCNode* self, GCNode* new_anchor, int linkCount) {  
    FOR_EACH_CONTRACTED_LINK(self->_anchors) {
        GCNode* anchor = iter._link->_endpoint;
        FOR_EACH_CONTRACTED_LINK(new_anchor->_anchors) {
            GCNode* endpoint = iter._link->_endpoint;
            if (endpoint == anchor) {
                Cll_remove(self->_anchors, anchor, iter._link->_linkCount);
            } 
        }
    }
    Cll_add(self->_anchors, new_anchor, linkCount);
}

void _replaceAnchorToOutgoingPaths(GCNode* self, GCNode* new_anchor, int linkCount) {  
    _replaceAnchorSetToNewAnchor(self, new_anchor, linkCount);
    FOR_EACH_CONTRACTED_LINK(self->_destinations) {
        GCNode* dest = iter._link->_endpoint;
        if (dest->_level <= new_anchor->_level) {
            _replaceAnchorToOutgoingPaths(dest, new_anchor, 1);
        }
    }
}


void _levelUpNode(GCNode* self, int newLevel) {
    int oldLevel = self->_level;
    TrackContext context;
    context._node = self;
    context._cntMarked = 0;
    context._inProgressLevelUp = true;

    self->_level = newLevel;
    do {
        newLevel = self->_level;
        FOR_EACH_CONTRACTED_LINK(self->_anchors) {
            int count = iter._link->_linkCount;
            if (count == 0) continue;

            GCNode* anchor = iter._link->_endpoint;
            int level = anchor->_level;
            if (level < self->_level) {
                if (level < oldLevel) {
                    Cll_removeFast(self->_anchors, iter._link);
                } else {
                    iter._link->_linkCount = 0;
                }
                _addDestinationToIncomingPaths(anchor, count, &context);
            }
        }
    } while(self->_level > newLevel); // 순환참조 발생 확인.

    _clearMarked(&context);

    LinkArray tmpArray;
    LinkArray* anchors = LINK_ARRAY_OF(self->_anchors, tmpArray);
    FOR_EACH_CONTRACTED_LINK(self->_destinations) {
        GCNode* dest = iter._link->_endpoint;
        // self->_level 보다 높은 destination 정리할 필요가 없다.
        if (dest->_level <= newLevel) {
            _replaceAnchorToOutgoingPaths(dest, self, iter._link->_linkCount);
            Cll_removeFast(self->_destinations, iter._link);
        }
    }
    context._cntMarked = 0;

    FOR_EACH_CONTRACTED_LINK(self->_anchors) {
        if (iter._link->_linkCount) {
            Cll_removeFast(self->_anchors, iter._link);
        } else {
            assert(iter._link->_endpoint->_level >= newLevel);
        }
    }
}


void _addAnchorToNode(GCNode* anchor, GCNode* node, int count) {
    Cll_add(node->_anchors, anchor, count);
}

void _removeAnchorFromNode(GCNode* anchor, GCNode* node, int count) {
    Cll_remove(node->_anchors, anchor, count);
}

void _addDestinationsToNode(GCNode* node, LinkArray* destinations) {
    FOR_EACH_CONTRACTED_LINK(destinations) {
        Cll_add(node->_destinations, iter._link->_endpoint, iter._link->_linkCount);
    }
}

BOOL _removeDestinationsFromNode(GCNode* node, GCNode* target, LinkArray* destinations) {
    if (node->_flags & IS_DIRTY_ANCHOR) {
        // destinations 가 추가되지 않은 상태이다.
        Cll_remove(target->_anchors, node, 1);
        if (Py_DebugFlag) {
            FOR_EACH_CONTRACTED_LINK(destinations) {
                _Item* link = Cll_pointerOf(node->_destinations, iter._link->_endpoint);
                assert(link == NULL || link->_linkCount > iter._link->_linkCount);
            }
        }
        return false;
    } else {
        FOR_EACH_CONTRACTED_LINK(destinations) {
            Cll_remove(node->_destinations, iter._link->_endpoint, iter._link->_linkCount);
        }
        return true;
    }
}

void _updateDestinationsAndGetSourceNodes(GCNode* node, GCNode* target, LinkArray* destinations, TrackContext* context) {
    // context._updateAnchorConnection = _removeAnchorFromNode = Cll_remove;
    // context._updateDestinationConnections = _removeDestinationsFromNode;
    int level = node->_level;
    if (!context->_updateDestinationConnections(node, target, destinations)) return;

    FOR_EACH_CONTRACTED_LINK(node->_anchors) {
        GCNode* anchor = iter._link->_endpoint;
        if (anchor->_level > level) {
            FOR_EACH_CONTRACTED_LINK(destinations) {
                _updateConnections(anchor, iter._link->_endpoint, context);
            }
        }
        else if (anchor->_level == level) {
            assert(!_isMarked(anchor)); // => 동일 레벨의 분기+합류는 발생하지 않는다.(???)
            assert(anchor->_level == level);
            assert(anchor == context->_node);
            _addMarked(context, node);
            _updateDestinationsAndGetSourceNodes(anchor, target, destinations, context);
        } else {
            assert (anchor->_flags & IS_DIRTY_ANCHOR);
            assert (node->_flags & HAS_DIRTY_ANCHOR);
        }
    }
}

void _updateConnections(GCNode* from, GCNode* to, TrackContext* context) {
    // context._updateAnchorConnection = _removeAnchorFromNode = Cll_remove;
    // context._updateDestinationConnections = _removeDestinationsFromNode;

    int diff = from->_level - to->_level;
    if (diff == 0) {
        context->_updateAnchorConnection(from, to, 1);
        if (!Cll_isEmpty(to->_destinations)) {
            // DIRTY_ANCHOR 에 대한 처리는 _updateDestinationsAndGetSourceNodes 에서.
            _updateDestinationsAndGetSourceNodes(from, to, to->_destinations, context);
        }
    }
    else if (diff > 0) {
        context->_updateAnchorConnection(from, to, 1);
        if (!Cll_isEmpty(to->_destinations)) {
            FOR_EACH_CONTRACTED_LINK(to->_destinations) {
                _updateConnections(from, iter._link->_endpoint, context);
            }
        }
    } else {
        LinkArray tmpArray[2];
        _initTempLinks(tmpArray, to);
        _updateDestinationsAndGetSourceNodes(from, to, tmpArray, context);
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
    if (anchor->_level < self->_level) {
        _removeDestinationFromIncomingPaths(anchor, self);
    } else {
        _removeAnchorFromOutgoingPaths(self, anchor);
    }
}

static const MAX_ANCHORS = 1;
static void _connectAnchorFast(GCNode* anchor, GCNode* target) {
    /**
     * performance overhead ~= destination node 탐색 회수
     */
    Cll_add(target->_anchors, anchor, 1);
    if (anchor->_level < target->_level) {
        anchor->_flags |= IS_DIRTY_ANCHOR;
        if (target->_flags & HAS_DIRTY_ANCHOR) {
            return;
        }
        target->_flags |= HAS_DIRTY_ANCHOR;
        anchor = target;
    } else {
        /**
         * @brief 정상적인 anchor 이다. 즉, target 을 기점으로 역방향 탐색이 가능하다.
         * 단, anchor 와 target 의 level 이 동일하고, target->_destinations 의 수가 0보다 큰 경우,
         * anchor->_destinations 는 target->_destinations 정보가 누락된 상태가 된다.
         */
    }
    FOR_EACH_CONTRACTED_LINK(target->_destinations) {
        _connectAnchorFast(iter._link->_endpoint, anchor);
    }
}

void _disconnectAnchorFast_obsolete(GCNode* anchor, GCNode* target) {
    /**
     * performance overhead ~= anchor node 탐색 회수
     */
    int diff = anchor->_level < target->_level;
    if (anchor->_level < target->_level) {
        if (anchor->_flags & IS_DIRTY_ANCHOR) {
            assert(target->_flags & HAS_DIRTY_ANCHOR);
            Cll_remove(target->_anchors, anchor, 1);
        }
        return;
    } else {
        // 정상적인 anchor
    }
    
    Cll_remove(target->_anchors, anchor, 1);
    FOR_EACH_CONTRACTED_LINK(target->_destinations) {
        _disconnectAnchorFast(iter._link->_endpoint, anchor);
    }
}

void RT_onReferentChanged(GCNode* self, GCNode* erased, GCNode* assigned) {
    if (assigned == erased) return;

    if (assigned != NULL) {        
        _connectAnchorFast(self, assigned);
    }    

    if (erased != NULL) {
        TrackContext context = { ._node = self, ._cntMarked = 0, ._inProgressLevelUp = false };
        context._updateAnchorConnection = _removeAnchorFromNode;
        context._updateDestinationConnections = _removeDestinationsFromNode;
        _updateConnections(self, erased, &context);
        _clearMarked(&context);
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