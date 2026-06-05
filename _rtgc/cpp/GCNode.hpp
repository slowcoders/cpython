#ifndef GCOBJECT_HPP
#define GCOBJECT_HPP
#include "GCUtils.hpp"
#include <atomic>

#include <stdlib.h>
#include <vector>
#include <assert.h>

class GCObject;
class GCNode;
class AcyclicNode;
class TrackableNode;
class TransitNode;
class ContractedEndpoint;
class CircuitNode;

static const int MAX_DESTINATION_COUNT = 4; 
static const bool ENABLE_RT_CIRCULAR_GARBAGE_DETECTION = true;

/*
아래는 SystemRuntime 에서 메모리관리 기능을 사용하기 위하여 호출하는 가비지 컬렉터 API 이다.

*/

class GarbageCollector {
public:    
    static GCNode* createMemoryManagementNode(GCObject* obj);

    static void processRootVariableChange(GCObject* assigned, GCObject* erased);

    static void processFieldVariableChange(GCObject* owner, GCObject* assigned, GCObject* erased);

    static void detectCircuit(ContractedEndpoint* endpoint);

private:
    static void collectGarbage(GCNode* node);
};

/*
GCObject 본 예시에 따른 가비지 컬렉터를 이용하여 자동적인 메모리 관리기능을 갖춘 최상위 클래스이다.
GCObject 는 각각 고유한 GCNode 를 통하여 객체 노드간 연결 정보 및 가비지 판별 정보를 관리한다.
*/

class GCObject {
public:    
	typedef GCObject* PTR;
    const char* _id;

    GCNode* _node;

    GCObject() {
        this->_node = GarbageCollector::createMemoryManagementNode(this);
    }

    static bool isAcyclic(GCObject* obj) { return false; }

    static void reclaimObject(GCObject* obj) { printf("deleted %s\n", obj->_id); }

    static void getReferents(GCObject* obj, std::vector<GCObject*>& referents);

	static void replaceMemberVariable(GCObject* owner, GCObject** pField, GCObject* v) {
      GCObject* erased = *pField;
      *pField = v;
      GarbageCollector::processFieldVariableChange(owner, v, erased);
    }
};

/* 축약 연결 정보 */
struct ContractedLink {
    ContractedEndpoint* _endpoint;
    int _linkCount;

    ContractedLink() {}

    ContractedLink(ContractedEndpoint* endpoint, int count) {
        _endpoint = endpoint;
        _linkCount = count;
    }
};

/* 축약 연결 정보 목록 */
typedef std::vector<ContractedLink>  ContractedLinkSet;
enum GCNodeType { Acyclic, Transit, Endpoint, Circuit };

/* GCNode 는 각 객체 또는 객체 집합의 가비지 여부를 판별하기 위한 정보 구조체에 대한 추상적인 정의이다. 
   그 기본적인 구조는 아래와 같다.   
*/
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

    static TransitNode* asTransit(GCNode* n) { 
        return (n != NULL && n->getNodeType() == Transit) ? (TransitNode*)n : NULL; 
    }
    static ContractedEndpoint* asEndpoint(GCNode* n) { 
        return (n != NULL && n->getNodeType() == Endpoint) ? (ContractedEndpoint*)n : NULL; 
    }
};

/* 본 예시에서는 GCNode 를 아래와 같이 분류한다. 
   먼저, AcyclicNode 는 순환 참조 발생이 불가능한 객체의 메모리 관리를 위해 사용된다. 순환 참조 발생이 불가능하므로
   기존의 참조수 기반 방식만으로 처리가 가능하다.
   AcyclicNode 가 아닌 객체를 TrackableNode 로 구분하고, TrackableNode 는 ContractedEdpoint 와
   TransitNode 로 세분한다. TransitNode 는 ContractedEdpoint 를 연결하는 경유점에 해당한다.
   CitcuitNode 는 순환 참조가 발생한 객체의 집합에 해당하는 노드이며, ContractedEdpoint 의 일종이다.
*/

/* 아래는 순환 참조 발생이 불가능한 객체를 기존의 ReferenceCount 방식으로 가비지 여부를 판별하기 위한 
   AcyclicNode 이다. 이미 잘 알려진 방식이므로 별도의 설명은 생략한다.
 */
class AcyclicNode : public GCNode {
public:
    AcyclicNode(GCObject* obj) { this->_obj = obj; this->_refCount = 0; }
//---
    GCNodeType getNodeType() { return GCNodeType::Acyclic; }
    bool isGarbage() { return _refCount == 0; }

    void increaseGroundRefCount() { _refCount ++; }
    void decreaseGroundRefCount(int amount) { _refCount -= amount; }
    void addIncomingLink(GCObject* referrer) { _refCount ++; }
    void removeIncomingLink(GCObject* referrer) { _refCount --; }
    void removeGarbageReferrer(GCObject* referrer) { _refCount --; }
    CircuitNode* getCircuitContainer() { return NULL; }
//---
};

/* 아래는 AcyclicNode 가 아닌 객체, 즉 순환 참조 발생이 가능한 객체를 따로 구분한 TrackableNode 이다.
 */

class TrackableNode : public GCNode {
public:
    TrackableNode() {}

    static void addDestinatonToIncomingTrack(TrackableNode* node, ContractedEndpoint* destination);
    static void removeDestinatonFromIncomingTrack(TrackableNode* node, ContractedEndpoint* destination);  
};


/* 아래는 축약 연결점 노드의 내부 구조이다. 본 발_은 유향 그래프 상에서 두 개의 경로가 합류하는 지점에 
위치한 노드를 축약 연결점으로 분류하고, 축약 연결점이 아닌 모든 객체를 TrasitNode 로 구별한다.


  본 예시는 incoming-link 가 2개 이상인 객체를  축약 연결점 노드에 포함시킨다.
  보다 바람직하게는 두 개의 
 */
class ContractedEndpoint : public TrackableNode {
public:    
    ContractedLinkSet* _incomingLinks;
    CircuitNode* _parentCircuit;
    int _outgoingLinkCountInCircuit;

    ContractedEndpoint() {}

    GCNodeType getNodeType() { return GCNodeType::Endpoint; }

    static ContractedEndpoint* transform(TransitNode* transit);

    CircuitNode* getCircuitContainer() {
        return this->_parentCircuit;
    }

    void increaseGroundRefCount();

    void decreaseGroundRefCount(int delta);

    void addIncomingLink(GCObject* referrer);

    void removeIncomingLink(GCObject* referrer);

    void removeGarbageReferrer(GCObject* referrer) {}

    void addIncomingTrack(ContractedEndpoint* source, int linkCount);

    void removeIncomingTrack(ContractedEndpoint* source);

    void decreaseOutgoingLinkCountInCircuit();

    bool isGarbage();
};


// 경유점 노드.
class TransitNode : public TrackableNode {
public:
    TrackableNode* _referrer;
    ContractedLinkSet* _destinationLinks;

    TransitNode(GCObject* obj) {
        _obj = obj;
        _refCount = 0;
        _referrer = NULL;
        _destinationLinks = new ContractedLinkSet();
    }

    GCNodeType getNodeType() { return GCNodeType::Transit; }


    void increaseGroundRefCount();

    void decreaseGroundRefCount(int amount);

    void addIncomingLink(GCObject* newReferrer);

    void removeIncomingLink(GCObject* referrer);

    void removeGarbageReferrer(GCObject* referrer);

    bool isGarbage();

    ContractedEndpoint* getSourceOfIncomingTrack();

    CircuitNode* getCircuitContainer();

    void detectCircuitInDestinationlessPath();
};




struct CircuitNode : public ContractedEndpoint {
    GCNodeType getNodeType() { return GCNodeType::Circuit; }
};



#endif  // GCOBJECT_HPP
