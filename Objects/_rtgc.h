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
typedef struct _ContractedEndpoint ContractedEndpoint;
typedef struct _CircuitNode CircuitNode;
typedef struct _ContractedLink ContractedLink;
typedef struct _LinkArray LinkArray;
typedef struct _CircuitNode CircuitNode;

typedef int BOOL;

static const int MAX_DESTINATION_COUNT = 4; 
static const int ENABLE_RT_CIRCULAR_GARBAGE_DETECTION = 1;

#define RTNode_Header()         \
    int32_t _linkRefCount;          \
    int32_t _groundRefCount;          \
    enum GCNodeType _nodeType;

/*
GarbageCollector
*/
struct _GCNode {
    RTNode_Header() 
};

struct _RT_Methods {
    BOOL (*isGarbage)(GCNode* self);
    void (*increaseGroundRefCount)(GCNode* self);
    BOOL (*decreaseGroundRefCount)(GCNode* self);
    void (*addIncomingLink)(GCNode* self, GCNode* referrer);
    BOOL (*removeIncomingLink)(GCNode* self, GCNode* referrer);
    void (*removeGarbageReferrer)(GCNode* self, GCObject* referrer);
    CircuitNode* (*getCircuitContainer)(GCNode* self);
};

extern const struct _RT_Methods _rt_vtables[5]; 
#define NUM_ARGS(...)  (sizeof((int[]){0, ##__VA_ARGS__})/sizeof(int)-1)

#define RT_INVOKE_V(method, self) \
    _rt_vtables[self->_nodeType].method(self)

#define RT_INVOKE(method, self, ...) \
    _rt_vtables[self->_nodeType].method(self, __VA_ARGS__)


inline GCNode* RT_getGCNode(GCObject* obj)  { return (GCNode*)obj; }
inline GCObject* RT_getObject(GCNode* node) { return (GCObject*)node; }

void RT_detectCircuit(ContractedEndpoint* endpoint);
void RT_collectGarbage(GCNode* node, void* dealloc);
inline void RT_markDestroyed(GCNode* node) { node->_nodeType = Destroyed; }

inline void RT_increaseGroundRefCount(GCObject* assigned) {
    GCNode* node = RT_getGCNode(assigned);
    RT_INVOKE_V(increaseGroundRefCount, node);
}

inline void RT_decreaseGroundRefCount(GCObject* erased) {
    GCNode* node = RT_getGCNode(erased);
    if (RT_INVOKE_V(decreaseGroundRefCount, node)) {
        RT_collectGarbage(node, NULL);
    }
}

inline void RT_decreaseGroundRefCountEx(GCObject* erased, void* dealloc) {
    GCNode* node = RT_getGCNode(erased);
    if (RT_INVOKE_V(decreaseGroundRefCount, node)) {
        RT_collectGarbage(node, dealloc);
    }
}

inline void RT_onFieldAssigned(GCObject* referrer, GCObject* assigned) {
    if (assigned != NULL && assigned != referrer) {
        GCNode* node = RT_getGCNode(assigned);
        RT_INVOKE(addIncomingLink, node, referrer);
    }
}

inline void RT_onFieldErased(GCObject* referrer, GCObject* erased) {
    if (erased != NULL && erased != referrer) {
        GCNode* node = RT_getGCNode(erased);
        if (RT_INVOKE(removeIncomingLink, node, referrer)) {
            RT_collectGarbage(node, NULL);
        }
    }
}

/*
GCObject
*/

inline BOOL isAcyclic(GCObject* obj) { return RT_getGCNode(obj)->_nodeType == Acyclic; }

inline void reclaimObject(GCObject* obj) { printf("deleted %p\n", obj); }

int getReferents(GCObject* obj, GCObject** referents, int max_count);



/* 축약 연결 정보 목록 */
typedef struct {
    int _count;
    ContractedLink* _links;
} ContractedLinkSet;



/*
AcyclicNode
*/

BOOL AC_isGarbage(GCNode* self) { return self->_linkRefCount == 0; }
void AC_increaseGroundRefCount(GCNode* self) { self->_linkRefCount ++; }
void AC_decreaseGroundRefCount(GCNode* self, int amount) { self->_linkRefCount -= amount; }
void AC_addIncomingLink(GCNode* self, GCObject* referrer) { self->_linkRefCount ++; }
void AC_removeIncomingLink(GCNode* self, GCObject* referrer) { self->_linkRefCount --; }
void AC_removeGarbageReferrer(GCNode* self, GCObject* referrer) { self->_linkRefCount --; }
CircuitNode* AC_getCircuitContainer(GCNode* self) { return NULL; }


/* 아래는 AcyclicNode 가 아닌 객체, 즉 순환 참조 발생이 가능한 객체를 따로 구분한 TrackableNode 이다.
TrackableNode
 */

void TX_addDestinatonToIncomingTrack(TrackableNode* node, ContractedEndpoint* destination);
void TX_removeDestinatonFromIncomingTrack(TrackableNode* node, ContractedEndpoint* destination);  

/*
ContractedEndpoint
*/
typedef struct _ContractedEndpoint {
    RTNode_Header()
    LinkArray* _incomingLinks;
    CircuitNode* _parentCircuit;
    int _outgoingLinkCountInCircuit;
} ContractedEndpoint;

typedef struct _CircuitNode {
    int _linkRefCount;
    int _linkRefCountInCircuit;
    int _groundRefCount;
    int _cid;
} CircuitNode;

ContractedEndpoint* EP_transform(TransitNode* transit);

inline CircuitNode* EP_getCircuitContainer(ContractedEndpoint* self) {
    return self->_parentCircuit;
}

void EP_increaseGroundRefCount(ContractedEndpoint* self, int count);

void EP_decreaseGroundRefCount(ContractedEndpoint* self, int count);

void EP_addIncomingLink(ContractedEndpoint* self, GCObject* referrer);

void EP_removeIncomingLink(ContractedEndpoint* self, GCObject* referrer);

void EP_removeGarbageReferrer(ContractedEndpoint* self, GCObject* referrer);

void EP_addIncomingTrack(ContractedEndpoint* self, ContractedEndpoint* source, int linkCount);

void EP_removeIncomingTrack(ContractedEndpoint* self, ContractedEndpoint* source);

void EP_decreaseOutgoingLinkCountInCircuit(ContractedEndpoint* self);

BOOL EP_isGarbage(ContractedEndpoint* self);


// 경유점 노드.
struct _TransitNode {
    RTNode_Header()
    TrackableNode* _referrer;
    LinkArray* _destinationLinks;
};

void TR_increaseGroundRefCount(TransitNode* self);

void TR_decreaseGroundRefCount(TransitNode* self);

void TR_addIncomingLink(TransitNode* self, GCNode* newReferrer);

void TR_removeIncomingLink(TransitNode* self, GCNode* referrer);

void TR_removeGarbageReferrer(TransitNode* self, GCObject* referrer);

BOOL TR_isGarbage(TransitNode* self);

ContractedEndpoint* TR_getSourceOfIncomingTrack(TransitNode* self);

CircuitNode* TR_getCircuitContainer(TransitNode* self);

void TR_detectCircuitInDestinationlessPath(TransitNode* self);




// struct CircuitNode : public ContractedEndpoint {
//     GCNodeType getNodeType() { return GCNodeType::Circuit; }
// };



#endif  // GCOBJECT_HPP
