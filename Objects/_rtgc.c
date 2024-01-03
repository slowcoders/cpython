#include "_rtgc.h"

#if 0
/**
 * NodeType 별 필수 항목
 * 0) Off-road Node		: _rootRefCount, _objRefCount, _referrer  --> Linear cluster = directed acyclic graph region.
 * 1) Linear Node 		: _rootRefCount, _objRefCount, _referrer, _destination
 * 2) Last Linear Node 	: 사용하지 않는다. LinearPath 가 너무 긴 경우, 중간에 다수의 Station 을 생성하여 prevStation 검색 효율을 높힌다.
 * 3) Convergence		: _rootRefCount, _objRefCount, _inverseRoutes, _destination
 * 4) Divergence		: _rootRefCount, _objRefCount, _referrer, _destination | _parentCircuit.
 * 5) Cross				: _rootRefCount, _objRefCount, _inverseRoutes, _destination | _parentCircuit.
 * 7) Acyclic			: _refCount
 */
/*

GCObject {
	_isAcyclic : 1;
	_maybeAcyclic : 1;
	_rootRefCount: 3;
	(GCNode & Hashcode) | extendedAcyclicRefCount : 31;
}

AcyclicNode {
	uint32_t	_refCount;
}

GCNode {
	struct {
		int64_t _isUnstable: 1;
		int64_t _isGarbage: 1;
		int64_t _isYoungRoot: 1;
		int64_t (GCObject*) _obj: 40;  // (GCObject*) 8T
        // ----- //
		int64_t _contextFlag: 1;
		int64_t _dirtyReferrerPoints: 1;
		int64_t _isTrackableOrDestroyed: 1;
        int64_t _nodeType: 2;

		int64_t _unused: 16;
	};
    int64_t _nodeData;
	// int32_t _referrer_or_prevStationInfo;  // 2G ea (31 bit)
	// int32_t _destinaton_or_parentCircuit_or_cntDestination; // 2G ea (31 bit, parentCircuit 과 cntDestination 은 30 bit)
}


TransitNode : public GCNode {
	struct {
		int64_t _shared: 48;
		int64_t _rootRefCount: 8;
		int64_t _destLinkCounts: 8; // 2*4
	};
    CachedNodeList          _destinations;
	CompressedPtr<GCNode>   _referrer;
}

ContractedEndpoint : public GCNode {
	struct {
		int64_t _shared: 48;
		int64_t _rootRefCount: 16;
	};
    CompressedPtr<CircuitNode> _parentCircuit;
    CompressedPtr<ContractedLink> _destinationLinks;
}



/*
 기반 참조수 변경 처리.
 한 객체의 기반 참조가 변경된 경우, 해당 객체가 속한 GCNode 의 타입에 따라 처리 방식이 달라진다.
 AcyclicNode 는 기존의 참조수 처리 방식을 사용하다.
 경유점 노드 또한 기존의 참조수 처리 방식과 동일하게 자신의 참조수를 변경한다. 순환 참조 노드 탐지와 동시에 순환 노드에 대한 외부 기반 참조수
 즉각 파악하기 윈하는 경우, 해당 경유점을 통과하여 도달할 수 있는 축약 연결점 객체, 즉 목표점의 기반 참조수 또한 변경하는 과정으로 추가적으로 수행할 수 있다.
 상기 목표점의 참조수를 미리 변경하면, 나중에 순환 참조 노드 탐지와 동시에 순환 노드에 대한 외부 참조수를 즉각적으로 파악할 수 있는 장점이 있다.
*/

void GarbageCollector::processRootVariableChange(GCObject* assigned, GCObject* erased) {
    if (assigned != NULL) assigned->_node->increaseGroundRefCount();
    if (erased != NULL) erased->_node->decreaseGroundRefCount(1);
}


void TransitNode::increaseGroundRefCount() {
    if (this->_refCount ++ == 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
        for (auto link : *_destinationLinks) {
            link._endpoint->increaseGroundRefCount();
        }
    }
}

void TransitNode::decreaseGroundRefCount(int amount) {
    if ((this->_refCount -= amount) == 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
        for (auto link : *_destinationLinks) {
            link._endpoint->decreaseGroundRefCount(1);
        }
    }
}

void ContractedEndpoint::increaseGroundRefCount() {
    if (this->_refCount ++ == 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
        if (this->_parentCircuit != NULL) {
            this->_parentCircuit->_refCount ++;
        }
    }
}

void ContractedEndpoint::decreaseGroundRefCount(int delta) {
    if ((this->_refCount -= delta) == 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
        if (this->_parentCircuit != NULL) {
            this->_parentCircuit->_refCount --;
        }
    }
}

/*
 참조 연결 생성 삭제 처리.
 두 객체 간의 참조 연결이 생성되거나 삭제된 경우, 해당 객체가 속한 GCNode 의 타입에 따라 처리 방식이 달라진다.
 AcyclicNode 는 기존의 참조수 처리 방식을 사용하다.
 경유점 노드의 경우, 자신의 객체 참조수 또는 참조자 수가 시스템이 정한 숫자보다 작으면 목표점을 추가하고,
 그 보다 큰 경우에는 해당 경유점 노드를 축약 연결점 노드로 변경한다.
 아래의 예시는 경유점 노드의 최대 객체 참조수를 1로 설정한 경우를 처리하는 예시이다. 단일 연결만으로 허용하므로,
 참조 연결수(_objLinkCount) 대신에 _referrer 필드만을 사용하여 축약 연결점 생성 조건을 판별한다.
*/


void TransitNode::addIncomingLink(GCObject* newReferrer) {
    auto oldReferrer = this->_referrer;
    if (oldReferrer == NULL) {
        this->_referrer = (TrackableNode*)newReferrer->_node;
        if (!_destinationLinks->empty()) {
            ContractedEndpoint* oldSource = this->getSourceOfIncomingTrack();
            for (auto link : *_destinationLinks) {
                link._endpoint->removeIncomingTrack(oldSource);
                addDestinatonToIncomingTrack(_referrer, link._endpoint);
            }
        } else {
            this->detectCircuitInDestinationlessPath();
        }
    } else {
        auto oldDestinations = this->_destinationLinks;
        auto stopover = ContractedEndpoint::transform(this);
        for (auto link : *oldDestinations) {
            removeDestinatonFromIncomingTrack(oldReferrer, link._endpoint);
        }
        addDestinatonToIncomingTrack(oldReferrer, stopover);
        for (auto link : *oldDestinations) {
            // newReferrer->addDestinatonToIncomingTrack() 전에 decreaseGroundRefCount 처리->
            link._endpoint->decreaseGroundRefCount(stopover->_refCount);
            link._endpoint->addIncomingTrack(stopover, link._linkCount);
        }
        addDestinatonToIncomingTrack((TrackableNode*)newReferrer->_node, stopover);
        delete oldDestinations;
    }
}

/*
축약점 노드에 대한 참조 연결이 발생한 경우, 참조자를 종착점으로 하는 진입 트랙에 속한 객체 들에
축약점 노드를 축약 경로 목표점으로 추가한다. 
*/
void ContractedEndpoint::addIncomingLink(GCObject* referrer) {
    addDestinatonToIncomingTrack((TrackableNode*)referrer->_node, this);
}


/*
경유점 노드와 한 참조자 객체와의 참조 연결이 삭제되면, 참조자를 종착점으로 하는 진입 트랙에 속한 객체 들에
상기 경유점 노드를 경유하는 축약 경로 목표점들을 삭제한다.
*/
void TransitNode::removeIncomingLink(GCObject* referrer) {
    assert(referrer != NULL && referrer->_node == _referrer);
    this->_referrer = NULL;
    for (auto link : *_destinationLinks) {
        removeDestinatonFromIncomingTrack(_referrer, link._endpoint);
    }
}

void ContractedEndpoint::removeIncomingLink(GCObject* referrer) {
    removeDestinatonFromIncomingTrack((TrackableNode*)referrer->_node, this);
}


/*
진입 트랙에 목표점 추가.
지정된 추적가능 노드로 부터 역방향으로 경로를 추적하여 다른 축약점 노드(NULL 포함)을 발견할 때 까지
경유점 노드에 목표점을 추가한다. 또한 트랙의 진입점을 발견하면 목표점 노드에 새로운 진입 경로를 추가한다.
*/
void TrackableNode::addDestinatonToIncomingTrack(TrackableNode* node, ContractedEndpoint* destination) {
    for (TransitNode* transit; (transit = asTransit(node)) != NULL; ) {
        for (auto link : *transit->_destinationLinks) {
            if (link._endpoint == destination) {
                link._linkCount ++; 
                return;
            }
        }

        if (transit->_destinationLinks->size() >= MAX_DESTINATION_COUNT) {
            auto endpoint = ContractedEndpoint::transform(transit);
            destination->addIncomingTrack(endpoint, 1);
            for (auto link : *transit->_destinationLinks) {
                removeDestinatonFromIncomingTrack(transit->_referrer, link._endpoint);
                link._endpoint->addIncomingTrack(endpoint, 1);
            }
            destination = endpoint;
        } else {
            transit->_destinationLinks->push_back(ContractedLink(destination, 1));
        }
        node = transit->_referrer;
    }
    destination->addIncomingTrack((ContractedEndpoint*)node, 1);
}

/*
진입 트랙에 목표점 제거.
지정된 추적가능 노드로 부터 역방향으로 경로를 추적하여 다른 축약점 노드(NULL 포함)을 발견할 때 까지
경유점 노드에 목표점을 추가한다. 또한 트랙의 진입점을 발견하면 목표점 노드에 새로운 진입 경로를 추가한다.
*/
void TrackableNode::removeDestinatonFromIncomingTrack(TrackableNode* node, ContractedEndpoint* destination) {
    for (TransitNode* transit; (transit = asTransit(node)) != NULL; ) {
        auto links = transit->_destinationLinks;
        for (int i = links->size(); --i >= 0;) {
            auto link = links->at(i);
            if (link._endpoint == destination) {
                if (--link._linkCount > 0) return;
                links->erase(links->begin() + i);
            }
        }
        node = transit->_referrer;
    }
    destination->removeIncomingTrack((ContractedEndpoint*)node);
}    


/*
가비지 객체에 의한 참조 삭제.
*/
void TransitNode::removeGarbageReferrer(GCObject* referrer) { 
    assert(referrer != NULL && referrer->_node == _referrer);
    this->_referrer = NULL;
}

void ContractedEndpoint::removeGarbageReferrer(GCObject* referrer) {}



bool TransitNode::isGarbage() {
    return this->_refCount == 0 && this->_referrer == NULL;
}

ContractedEndpoint* TransitNode::getSourceOfIncomingTrack() {
    TrackableNode* node = this->_referrer;
    for (TransitNode* transit; (transit = asTransit(node)) != NULL; ) {
        node = transit->_referrer;
    }
    return (ContractedEndpoint*)node;
}

CircuitNode* TransitNode::getCircuitContainer() {
    auto originNode = this->getSourceOfIncomingTrack();
    if (originNode != NULL && originNode->_parentCircuit != NULL) {
        for (auto link : *_destinationLinks) {
            if (link._endpoint->_parentCircuit == originNode->_parentCircuit) {
                return originNode->_parentCircuit;
            }
        }
    }
    return NULL;
}

void TransitNode::detectCircuitInDestinationlessPath() {
    if (!_destinationLinks->empty()) return;
    auto node = this->_referrer;
    for (TransitNode* transit; (transit = asTransit(node)) != NULL; ) {
        if (!transit->_destinationLinks->empty()) break; 
        node = transit->_referrer;
        if (transit == this) {
            auto destination = ContractedEndpoint::transform(transit);            
            addDestinatonToIncomingTrack((TrackableNode*)node, destination);
            return;
        }
    }
}    


ContractedEndpoint* ContractedEndpoint::transform(TransitNode* transit) {
    ContractedEndpoint* self = new ((void*)transit) ContractedEndpoint();
    self->_parentCircuit = NULL;
    self->_incomingLinks = new ContractedLinkSet();
    return self;
}

void ContractedEndpoint::addIncomingTrack(ContractedEndpoint* source, int linkCount) {
    if (source == NULL) {
        if (this->_refCount ++ > 0) return;
    } else {
        for (auto link : *_incomingLinks) {
            if (link._endpoint == source) {
                link._linkCount += linkCount;
                return;
            }
        }
        this->_incomingLinks->push_back(ContractedLink(source, linkCount));
        GarbageCollector::detectCircuit(source);
    }

    if (ENABLE_RT_CIRCULAR_GARBAGE_DETECTION && this->_parentCircuit != NULL && source != NULL && source->_parentCircuit != this->_parentCircuit) {
        this->_parentCircuit->_refCount ++;
    }                
}

void ContractedEndpoint::removeIncomingTrack(ContractedEndpoint* source) {
    if (source == NULL) {
        if (--this->_refCount > 0) return;
    }
    else {
        for (int idx = _incomingLinks->size(); --idx >= 0; ) {
            auto link = _incomingLinks->at(idx);
            if (link._endpoint == source) {
                if (--link._linkCount > 0) return;
                _incomingLinks->erase(_incomingLinks->begin() + idx); 
                break;
            }
        }
    }

    if (_parentCircuit != NULL) {
        if (source != NULL && source->_parentCircuit == _parentCircuit) {
            source->decreaseOutgoingLinkCountInCircuit();
        } else {
            _parentCircuit->_refCount --;
        }
    }
}

void ContractedEndpoint::decreaseOutgoingLinkCountInCircuit() {
    if (--_outgoingLinkCountInCircuit > 0) return;
    
    auto circuit = this->_parentCircuit;
    this->_parentCircuit = NULL;
    for (auto link : *_incomingLinks) {
        if (link._endpoint->_parentCircuit == circuit) {
            link._endpoint->decreaseOutgoingLinkCountInCircuit();
        }
    }
}

bool ContractedEndpoint::isGarbage() {
    if (this->_refCount > 0) return false;
    if (this->_incomingLinks->size() == 0) return true;
    return this->_parentCircuit != NULL && this->_parentCircuit->_refCount == 0;
}

class CircuitDetector {
    std::vector<ContractedEndpoint*>  _visitedNodes;
    std::vector<ContractedEndpoint*>  _traceStack;
    CircuitNode* _circuitInStack;

public:
    CircuitDetector() { 
        _circuitInStack = NULL;
    }

    static int indexOf(std::vector<ContractedEndpoint*>& v, ContractedEndpoint* endpoint) {
        for (int i = v.size(); --i >= 0;) {
            if (v[i] == endpoint) return i;
        }
        return -1;
    }

    void checkCyclic(ContractedEndpoint* endpoint) {
        if (indexOf(_visitedNodes, endpoint) >= 0) return;

        auto idx = indexOf(_traceStack, endpoint);
        if (idx >= 0) {
            auto circuit = _circuitInStack;
            if (circuit == NULL) {
                circuit = _circuitInStack = new CircuitNode();
            }
            for (int i = _traceStack.size(); --i >= idx;) {
                auto node = _traceStack.at(i);
                if (node->_parentCircuit == NULL) {
                    node->_parentCircuit = circuit;
                    if (node->_refCount > 0) {
                        circuit->_refCount ++;
                    }
                }
            }
            return;
        }

        auto stackDepth = _traceStack.size();
        _traceStack.push_back(endpoint);
        endpoint->_parentCircuit = NULL;
        endpoint->_outgoingLinkCountInCircuit = 0;
        int externalLinkCount = 0;
        for (auto link : *endpoint->_incomingLinks) {
            this->checkCyclic(link._endpoint);
            auto circuit = link._endpoint->_parentCircuit;
            if (circuit != NULL && circuit == endpoint->_parentCircuit) {
                link._endpoint->_outgoingLinkCountInCircuit ++;
            } else {
                externalLinkCount ++;
            }
        }
        if (endpoint->_parentCircuit != NULL) {
            endpoint->_parentCircuit->_refCount += externalLinkCount;
        } else {
            _traceStack.resize(stackDepth);
            _circuitInStack = NULL;
        }
        _visitedNodes.push_back(endpoint);
    }
};



GCNode* GarbageCollector::createMemoryManagementNode(GCObject* obj) {
    return GCObject::isAcyclic(obj) ? (GCNode*)new AcyclicNode(obj) : (GCNode*)new TransitNode(obj);
}

void GarbageCollector::processFieldVariableChange(GCObject* owner, GCObject* assigned, GCObject* erased) {
    if (assigned != NULL && assigned != owner) assigned->_node->addIncomingLink(owner);
    if (erased != NULL && erased != owner) {
        GCNode* erasedNode = erased->_node;
        erasedNode->removeIncomingLink(owner);
        if (erasedNode->isGarbage()) {
            collectGarbage(erasedNode);
        }
    }
}

void GarbageCollector::detectCircuit(ContractedEndpoint* endpoint) {
        (new CircuitDetector())->checkCyclic(endpoint);
}


void GarbageCollector::collectGarbage(GCNode* node) {
    assert(node->isGarbage());
    auto obj = node->_obj;
    node->markDestroyed();
    std::vector<GCObject*> referents;
    obj->getReferents(obj, referents);
    for (auto referent : referents) {
        auto node = referent->_node;
        if (node->isDestroyed()) continue;
        node->removeGarbageReferrer(obj);
        if (node->isGarbage()) {
            collectGarbage(node);
        }
    }
}

#endif