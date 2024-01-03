#ifndef GCOBJECT_HPP
#define GCOBJECT_HPP

#include "Python.h"
#include <stdlib.h>
// #include <vector>
// #include <assert.h>

enum GCNodeType { Acyclic, Transit, Endpoint, Circuit };

typedef struct _object GCObject;
typedef struct _GCNode GCNode;
typedef struct _AcyclicNode AcyclicNode;
typedef struct _TrackableNode TrackableNode;
typedef struct _TransitNode TransitNode;
typedef struct _ContractedEndpoint ContractedEndpoint;
typedef struct _CircuitNode CircuitNode;
typedef struct _ContractedLink ContractedLink;
typedef struct _CircuitNode CircuitNode;

typedef int bool;

static const int MAX_DESTINATION_COUNT = 4; 
static const int ENABLE_RT_CIRCULAR_GARBAGE_DETECTION = 1;

/*
GarbageCollector
*/
struct _GCNode {
    int32_t _refCount;
    enum GCNodeType _nodeType;
};

GCNode* createMemoryManagementNode(GCObject* obj);

void processRootVariableChange(GCObject* assigned, GCObject* erased);

void processFieldVariableChange(GCObject* owner, GCObject* assigned, GCObject* erased);

void detectCircuit(ContractedEndpoint* endpoint);

void collectGarbage(GCNode* node);

inline GCNode* getGCNode(GCObject* obj) { return (GCNode*)obj; }
/*
GCObject
*/

inline bool isAcyclic(GCObject* obj) { return getGCNode(obj)->_nodeType == Acyclic; }

inline void reclaimObject(GCObject* obj) { printf("deleted %p\n", obj); }

int getReferents(GCObject* obj, GCObject** referents, int max_count);

// inline void replaceMemberVariable(GCObject* owner, GCObject** pField, GCObject* v) {
//     GCObject* erased = *pField;
//     *pField = v;
//     processFieldVariableChange(owner, v, erased);
// }


/* 축약 연결 정보 */
struct _ContractedLink {
    ContractedEndpoint* _endpoint;
    int _linkCount;
};

inline void initContractedLink(ContractedLink* self, ContractedEndpoint* endpoint, int count) {
    self->_endpoint = endpoint;
    self->_linkCount = count;
}


/* 축약 연결 정보 목록 */
typedef struct {
    int _count;
    ContractedLink* _links;
} ContractedLinkSet;

/* GCNode 는 각 객체 또는 객체 집합의 가비지 여부를 판별하기 위한 정보 구조체에 대한 추상적인 정의이다. 
   그 기본적인 구조는 아래와 같다.   
class GCNode {
public:
#ifdef DEBUG
    const char* _id;
#endif    
    GCObject* _obj;
    int       _refCount;

    GCNode() {}
    void markDestroyed() { this->_obj = NULL; }
    bool isDestroyed()   { return this->_obj == NULL; }

    virtual GCNodeType getNodeType() = 0;
    virtual void increaseGroundRefCount() = 0;
    virtual void decreaseGroundRefCount(int amount) = 0;
    virtual void addIncomingLink(GCObject* referrer) = 0;
    virtual void removeIncomingLink(GCObject* referrer) = 0;
    virtual void removeGarbageReferrer(GCObject* referrer) = 0;
    virtual bool isGarbage() = 0;
    virtual CircuitNode* getCircuitContainer() = 0;

};
inline GCNodeType getNodeType(GCNode* n) {
    return n->_nodeType;
}
*/


inline TransitNode* asTransit(GCNode* n) { 
    return (n != NULL && n->_nodeType == Transit) ? (TransitNode*)n : NULL; 
}
inline ContractedEndpoint* asEndpoint(GCNode* n) { 
    return (n != NULL && n->_nodeType == Endpoint) ? (ContractedEndpoint*)n : NULL; 
}

/*
AcyclicNode
*/

bool AC_isGarbage(GCNode* self) { return self->_refCount == 0; }

void AC_increaseGroundRefCount(GCNode* self) { self->_refCount ++; }
void AC_decreaseGroundRefCount(GCNode* self, int amount) { self->_refCount -= amount; }
void AC_addIncomingLink(GCNode* self, GCObject* referrer) { self->_refCount ++; }
void AC_removeIncomingLink(GCNode* self, GCObject* referrer) { self->_refCount --; }
void AC_removeGarbageReferrer(GCNode* self, GCObject* referrer) { self->_refCount --; }
CircuitNode* AC_getCircuitContainer(GCNode* self) { return NULL; }


/* 아래는 AcyclicNode 가 아닌 객체, 즉 순환 참조 발생이 가능한 객체를 따로 구분한 TrackableNode 이다.
TrackableNode
 */

void addDestinatonToIncomingTrack(TrackableNode* node, ContractedEndpoint* destination);
void removeDestinatonFromIncomingTrack(TrackableNode* node, ContractedEndpoint* destination);  

/*
ContractedEndpoint
*/
typedef struct _ContractedEndpoint {
    ContractedLinkSet* _incomingLinks;
    CircuitNode* _parentCircuit;
    int _outgoingLinkCountInCircuit;
} ContractedEndpoint;

typedef struct _CircuitNode {
    int _cid;
} CircuitNode;

ContractedEndpoint* EP_transform(TransitNode* transit);

inline CircuitNode* EP_getCircuitContainer(ContractedEndpoint* self) {
    return self->_parentCircuit;
}

void EP_increaseGroundRefCount(ContractedEndpoint* self);

void EP_decreaseGroundRefCount(ContractedEndpoint* self, int delta);

void EP_addIncomingLink(ContractedEndpoint* self, GCObject* referrer);

void EP_removeIncomingLink(ContractedEndpoint* self, GCObject* referrer);

void EP_removeGarbageReferrer(ContractedEndpoint* self, GCObject* referrer) {}

void EP_addIncomingTrack(ContractedEndpoint* self, ContractedEndpoint* source, int linkCount);

void EP_removeIncomingTrack(ContractedEndpoint* self, ContractedEndpoint* source);

void EP_decreaseOutgoingLinkCountInCircuit(ContractedEndpoint* self);

bool EP_isGarbage(ContractedEndpoint* self);


// 경유점 노드.
struct _TransitNode {
    TrackableNode* _referrer;
    ContractedLinkSet* _destinationLinks;
};


void TR_increaseGroundRefCount(TransitNode* self);

void TR_decreaseGroundRefCount(TransitNode* self, int amount);

void TR_addIncomingLink(TransitNode* self, GCObject* newReferrer);

void TR_removeIncomingLink(TransitNode* self, GCObject* referrer);

void TR_removeGarbageReferrer(TransitNode* self, GCObject* referrer);

bool TR_isGarbage(TransitNode* self);

ContractedEndpoint* TR_getSourceOfIncomingTrack(TransitNode* self);

CircuitNode* TR_getCircuitContainer(TransitNode* self);

void TR_detectCircuitInDestinationlessPath(TransitNode* self);




// struct CircuitNode : public ContractedEndpoint {
//     GCNodeType getNodeType() { return GCNodeType::Circuit; }
// };



#endif  // GCOBJECT_HPP
