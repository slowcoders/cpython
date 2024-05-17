#ifndef GCOBJECT_HPP
#define GCOBJECT_HPP

#include "Python.h"
#include <stdlib.h>
#include <assert.h>

enum GCNodeType { Acyclic, Transit, Endpoint, Circuit, Destroyed = -1 };

typedef struct _object GCObject;
typedef struct _GCNode GCNode;
typedef struct _AcyclicNode AcyclicNode;
typedef struct _TrackableNode TrackableNode;
typedef struct _TransitNode TransitNode;
typedef struct _CircuitNode CircuitNode;
typedef struct _ContractedLink ContractedLink;
typedef struct _LinkArray LinkArray;
typedef struct _CircuitNode CircuitNode;

typedef int BOOL;

static const int MAX_DESTINATION_COUNT = 4; 
static const int ENABLE_RT_CIRCULAR_GARBAGE_DETECTION = 1;

#define RTNode_Header()             \
    int32_t _groundRefCount;        \
    enum GCNodeType _nodeType;      \

/*
GarbageCollector
*/
struct _GCNode {
    int32_t _groundRefCount;
    int8_t  _level;
    enum GCNodeType _nodeType;
    LinkArray* _anchors;
    LinkArray* _destinations;
};

#define NUM_ARGS(...)  (sizeof((int[]){0, ##__VA_ARGS__})/sizeof(int)-1)

#define RT_INVOKE_V(method, self) \
    _rt_vtables[self->_nodeType].method(self)

#define RT_INVOKE(method, self, ...) \
    _rt_vtables[self->_nodeType].method(self, __VA_ARGS__)


static inline GCNode* RT_getGCNode(GCObject* obj)  { return (GCNode*)obj; }
static inline GCObject* RT_getObject(GCNode* node) { return (GCObject*)node; }

void RT_detectCircuit(GCNode* endpoint);
void RT_collectGarbage(GCNode* node, void* dealloc);
static inline void RT_markDestroyed(GCNode* node) { node->_nodeType = Destroyed; }


// static inline void RT_onFieldAssigned(GCObject* referrer, GCObject* assigned) {
//     if (assigned != NULL && assigned != referrer) {
//         GCNode* node = RT_getGCNode(assigned);
//         RT_INVOKE(addIncomingLink, node, referrer);
//     }
// }

// static inline void RT_onFieldErased(GCObject* referrer, GCObject* erased) {
//     if (erased != NULL && erased != referrer) {
//         GCNode* node = RT_getGCNode(erased);
//         if (RT_INVOKE(removeIncomingLink, node, referrer)) {
//             RT_collectGarbage(node, NULL);
//         }
//     }
// }

/*
GCObject
*/

// static inline BOOL isAcyclic(GCObject* obj) { return RT_getGCNode(obj)->_nodeType == Acyclic; }

// static inline void reclaimObject(GCObject* obj) { printf("deleted %p\n", obj); }

// int getReferents(GCObject* obj, GCObject** referents, int max_count);



/* 축약 연결 정보 목록 */
typedef struct {
    int _count;
    ContractedLink* _links;
} ContractedLinkSet;



/*
AcyclicNode
*/

BOOL AC_isGarbage(GCNode* self) { return self->_groundRefCount == 0; }
void AC_increaseGroundRefCount(GCNode* self) { self->_groundRefCount ++; }
void AC_decreaseGroundRefCount(GCNode* self, int amount) { self->_groundRefCount -= amount; }
void AC_addIncomingLink(GCNode* self, GCObject* referrer) { self->_groundRefCount ++; }
void AC_removeIncomingLink(GCNode* self, GCObject* referrer) { self->_groundRefCount --; }
void AC_removeGarbageReferrer(GCNode* self, GCObject* referrer) { self->_groundRefCount --; }


/* 아래는 AcyclicNode 가 아닌 객체, 즉 순환 참조 발생이 가능한 객체를 따로 구분한 TrackableNode 이다.
TrackableNode
 */

void EP_increaseGroundRefCount(GCNode* self, int count);
void EP_decreaseGroundRefCount(GCNode* self, int count);



GCNode* TR_getSourceOfIncomingTrack(TransitNode* self);

CircuitNode* TR_getCircuitContainer(TransitNode* self);

void TR_detectCircuitInDestinationlessPath(TransitNode* self);

void TX_addDestinatonToIncomingTrack(TrackableNode* node, GCNode* destination);
void TX_removeDestinatonFromIncomingTrack(TrackableNode* node, GCNode* destination);  

static inline void RT_increaseGroundRefCount(GCObject* assigned) {
    GCNode* node = RT_getGCNode(assigned);
    EP_increaseGroundRefCount(node, 1);
}

static inline void RT_decreaseGroundRefCount(GCObject* erased) {
    GCNode* node = RT_getGCNode(erased);
    EP_decreaseGroundRefCount(node, 1);
}

static inline void RT_decreaseGroundRefCountEx(GCObject* erased, void* dealloc) {
    GCNode* node = RT_getGCNode(erased);
    assert(false);
    // if (EP_decreaseGroundRefCount(node, 1)) {
    //     RT_collectGarbage(node, dealloc);
    // }
}




#endif  // GCOBJECT_HPP
