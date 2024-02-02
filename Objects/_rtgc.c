#include "_rtgc.h"
#include "_rtgc_util.h"

static const BOOL true = 1;
static const BOOL false = 0;
static const BOOL FAST_UPDATE_DESTNATION_LINKS = true;
static const CircuitNode* NoCircuit = (CircuitNode*)-1;

inline TransitNode* asTransit(GCNode* n) { 
    return (n != NULL && n->_nodeType == Transit) ? (TransitNode*)n : NULL; 
}
inline ContractedEndpoint* asEndpoint(GCNode* n) { 
    return (n != NULL && n->_nodeType == Endpoint) ? (ContractedEndpoint*)n : NULL; 
}
inline void TR_checkType(TransitNode* self) { assert(asTransit((GCNode*)self) != NULL); }
inline void EP_checkType(TransitNode* self) { assert(asEndpoint((GCNode*)self) != NULL); }

static CircuitNode* TR_replaceDestinationLinksOfIncomingPath(TransitNode* node, LinkArray* addedLinks, LinkArray* erasedLinks);

#if 0
/**
 * NodeType 별 필수 항목
 * 0) Off-road Node		: _rootRefCount, _objRefCount, _referrer  --> Linear cluster = directed acyclic graph region.
 * 1) Linear Node 		: _rootRefCount, _objRefCount, _referrer, _destination
 * 2) Last Linear Node 	: 사용하지 않는다. LinearPath 가 너무 긴 경우, 중간에 다수의 Station 을 생성하여 prevStation 검색 효율을 높힌다.
 * 3) Convergence		: _rootRefCount, _objRefCount, _inverseRoutes, _destination
 * 4) Divergence		: _rootRefCount, _objRefCount, _referrer, _destination | _parentCircuit.
 * 5) Cross				: _rootRefCount, _objRefCount, _inverseRoutes, _destination | _parentCircuit.
 * 7) Acyclic			: _linkRefCount
 */
/*

GCObject {
	_isAcyclic : 1;
	_maybeAcyclic : 1;
	_rootRefCount: 3;
	(GCNode & Hashcode) | extendedAcyclicRefCount : 31;
}

AcyclicNode {
	uint32_t	_linkRefCount;
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
                    if (node->_linkRefCount > 0) {
                        circuit->_linkRefCount ++;
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
            self->checkCyclic(link._endpoint);
            auto circuit = link._endpoint->_parentCircuit;
            if (circuit != NULL && circuit == endpoint->_parentCircuit) {
                link._endpoint->_outgoingLinkCountInCircuit ++;
            } else {
                externalLinkCount ++;
            }
        }
        if (endpoint->_parentCircuit != NULL) {
            endpoint->_parentCircuit->_linkRefCount += externalLinkCount;
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




#endif


static void _initTempLinks(LinkArray* tmpArray, ContractedEndpoint* endpoint) {
    ContractedLink* link = (ContractedLink*)(tmpArray + 1);
    link->_endpoint = (ContractedEndpoint*)endpoint;
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
        return ((TransitNode*)node)->_destinationLinks;
    }
    else {
        assert(node->_nodeType == Endpoint);
        _initTempLinks(tmpArray, (ContractedEndpoint*)node);
        return tmpArray;
    }
}

void TR_increaseGroundRefCount(TransitNode* self) {
    TR_checkType(self);
    if (self->_groundRefCount++ == 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
        FOR_EACH_CONTRACTED_LINK(self->_destinationLinks) {
            EP_increaseGroundRefCount(iter._link->_endpoint, 1);
        }
    }
}

void TR_decreaseGroundRefCount(TransitNode* self) {
    TR_checkType(self);
    if (--self->_groundRefCount == 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
        FOR_EACH_CONTRACTED_LINK(self->_destinationLinks) {
            EP_decreaseGroundRefCount(iter._link->_endpoint, 1);
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


void TR_addIncomingLink(TransitNode* self, GCNode* newReferrer) {
    TR_checkType(self);
    auto oldReferrer = self->_referrer;
    if (oldReferrer == NULL) {
        self->_referrer = newReferrer;
        TR_detectCircuitInDestinationlessPath(self);
    } else {
        LinkArray tmpAddedLinks[2]; 
        LinkArray* oldDestinations = self->_destinationLinks;
        ContractedEndpoint* stopover = EP_transform(self);
        _initTempLinks(tmpAddedLinks, stopover);
        // old-referrer 의 incoming-path 에서 현재 detinations 를 삭제하고 stopover(=self) 를 추가한다.
        int orgRefCount = stopover->_linkRefCount;
        CircuitNode* circuit = TR_replaceDestinationLinksOfIncomingPath(oldReferrer, oldDestinations, tmpAddedLinks);
        if (circuit != NULL) {
            stopover->_parentCircuit = circuit;
            circuit->_linkRefCount += stopover->_linkRefCount;
        }
        // circuit->_linkRefCount = sum of _linkRefCount of the endpoints in the circuit + groundRefCount of the nodes in circuit; 
        FOR_EACH_CONTRACTED_LINK(oldDestinations) {
            EP_addIncomingTrack(iter._link->_endpoint, stopover, iter._link->_linkCount);
        }
        if (oldDestinations != NULL && oldDestinations->_owner == self) {
            Cll_delete(oldDestinations);
        }
    }
}

/*
경유점 노드와 한 참조자 객체와의 참조 연결이 삭제되면, 참조자를 종착점으로 하는 진입 트랙에 속한 객체 들에
상기 경유점 노드를 경유하는 축약 경로 목표점들을 삭제한다.
*/
void TR_removeIncomingLink(TransitNode* self, GCNode* referrer) {
    TR_checkType(self);
    assert(referrer == self->_referrer);
    self->_referrer = NULL;
}

static BOOL _isSameLinks(ContractedLink* linkA, ContractedLink* linkB) {
    return linkA->_endpoint  == linkB->_endpoint
        && linkA->_linkCount == linkB->_linkCount;
}

static CircuitNode* TR_replaceDestinationLinksOfIncomingPath(TransitNode* node, LinkArray* addedLinks, LinkArray* erasedLinks) {
    int cntAdded   = Cll_size(addedLinks);
    int cntRemoved = Cll_size(erasedLinks);
    if (cntAdded == cntRemoved) {
        if (cntAdded == 0 ||
            !memcmp(addedLinks->_items, addedLinks->_items, cntAdded * sizeof(ContractedLink))) {
            return NULL;
        }
        if (cntAdded == 2
        &&  _isSameLinks(&addedLinks->_items[0], &addedLinks->_items[1])
        &&  _isSameLinks(&addedLinks->_items[1], &addedLinks->_items[0])) {
            return NULL;
        }
    }

    GCNode* referrer;
    int countGroundNode = 0;
    for (int depth = 0;; depth ++) {
        LinkArray* replacedLinks = node->_destinationLinks;
        LinkArray* updatedLinks = updatedLinks;
        if (depth > 0) {
            assert(updatedLinks->_owner == node);
        } else if (updatedLinks->_owner != node) {
            updatedLinks = Cll_clone(addedLinks, node);
        } else {
            depth ++;
        }
        FOR_EACH_CONTRACTED_LINK(addedLinks) {
            ContractedLink* link0 = Cll_pointerOf(updatedLinks, iter._link->_endpoint);
            if (link0 == NULL) {
                Cll_push(updatedLinks, iter._link);
            } else {
                link0->_linkCount += iter._link->_linkCount;
            }
        }
        FOR_EACH_CONTRACTED_LINK(erasedLinks) {
            ContractedLink* link0 = Cll_pointerOf(updatedLinks, iter._link->_endpoint);
            assert(link0 != NULL);
            assert(link0->_linkCount >= iter._link->_linkCount);
            if ((link0->_linkCount -= iter._link->_linkCount) == 0) {
                Cll_removeFast(updatedLinks, link0);
            }
        }

        while (1) {
            if (node->_groundRefCount > 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
                countGroundNode ++;
            }
            referrer = node->_referrer;
            node = asTransit(referrer);
            if (node == NULL) goto done;
            if (node->_destinationLinks != replacedLinks) break;
            if (depth == 0) {
                node->_destinationLinks = updatedLinks;
            }
        }
    }

    done:
    if (referrer == NULL) return NoCircuit;

    ContractedEndpoint* startPoint = (ContractedEndpoint*)referrer;
    FOR_EACH_CONTRACTED_LINK(addedLinks) {
        EP_addIncomingTrack(iter._link->_endpoint, startPoint, 1);
        if (countGroundNode > 0) {
            EP_increaseGroundRefCount(iter._link->_endpoint, countGroundNode);
        }
    }
    FOR_EACH_CONTRACTED_LINK(erasedLinks) {
        EP_removeIncomingTrack(iter._link->_endpoint, startPoint);
        if (countGroundNode > 0) {
            EP_decreaseGroundRefCount(iter._link->_endpoint, countGroundNode);
        }
    }
    return startPoint->_parentCircuit;
}

void RT_onReferentChanged(GCNode* self, GCObject* erased, GCObject* assigned) {
    GCNode* erasedNode   = asTransit(RT_getGCNode(erased));
    GCNode* assignedNode = asTransit(RT_getGCNode(assigned));
    if (erasedNode != NULL) {
        TR_removeIncomingLink(erasedNode, self);
    }
    if (assignedNode != NULL) {
        TR_addIncomingLink(assignedNode, self);
    }    
    LinkArray tmpAddedLinks[2], tmpErasedLinks[2];
    LinkArray* addedLinks  = _getDestinationLinks(assignedNode, tmpAddedLinks);
    LinkArray* erasedLinks = _getDestinationLinks(erasedNode, tmpErasedLinks);

    if (self->_nodeType == Transit) {
        TransitNode* node = (TransitNode*)self;
        TR_replaceDestinationLinksOfIncomingPath(node, addedLinks, erasedLinks);
    } else {
        ContractedEndpoint* node = (ContractedEndpoint*)self;
        EP_checkType(node);
        FOR_EACH_CONTRACTED_LINK(addedLinks) {
            EP_addIncomingTrack(iter._link->_endpoint, node, 1);
        }
        FOR_EACH_CONTRACTED_LINK(erasedLinks) {
            EP_removeIncomingTrack(iter._link->_endpoint, node);
        }
    }
}


/*
가비지 객체에 의한 참조 삭제.
*/
void TR_removeGarbageReferrer(TransitNode* self, GCObject* referrer) { 
    TR_checkType(self);
    assert(referrer != NULL && RT_getGCNode(referrer) == self->_referrer);
    self->_referrer = NULL;
}

BOOL TR_isGarbage(TransitNode* self) {
    TR_checkType(self);
    return self->_linkRefCount == 0 && self->_referrer == NULL;
}

ContractedEndpoint* TR_getSourceOfIncomingTrack(TransitNode* self) {
    TR_checkType(self);
    TrackableNode* node = self->_referrer;
    for (TransitNode* transit; (transit = asTransit(node)) != NULL; ) {
        node = transit->_referrer;
    }
    return (ContractedEndpoint*)node;
}

CircuitNode* TR_getCircuitContainer(TransitNode* self) {
    TR_checkType(self);
    ContractedEndpoint* originNode = TR_getSourceOfIncomingTrack(self);
    if (originNode != NULL && originNode->_parentCircuit != NULL) {
        FOR_EACH_CONTRACTED_LINK(self->_destinationLinks) {
            if (iter._link->_endpoint->_parentCircuit == originNode->_parentCircuit) {
                return originNode->_parentCircuit;
            }
        }
    }
    return NULL;
}

void TR_detectCircuitInDestinationlessPath(TransitNode* self) {
    TR_checkType(self);
    if (!Cll_isEmpty(self->_destinationLinks)) return;
    auto node = self->_referrer;
    for (TransitNode* transit; (transit = asTransit(node)) != NULL; ) {
        if (!Cll_isEmpty(transit->_destinationLinks)) return;
        node = transit->_referrer;
        if (transit == self) {
            auto destination = EP_transform(transit);            
            TX_addDestinatonToIncomingTrack((TrackableNode*)node, destination);
            return;
        }
    }
}    

/*
진입 트랙에 목표점 추가.
지정된 추적가능 노드로 부터 역방향으로 경로를 추적하여 다른 축약점 노드(NULL 포함)을 발견할 때 까지
경유점 노드에 목표점을 추가한다. 또한 트랙의 진입점을 발견하면 목표점 노드에 새로운 진입 경로를 추가한다.
*/
void TX_addDestinatonToIncomingTrack(TrackableNode* node, ContractedEndpoint* destination) {
    for (TransitNode* transit; (transit = asTransit(node)) != NULL; ) {
        FOR_EACH_CONTRACTED_LINK(transit->_destinationLinks) {
            if (iter._link->_endpoint == destination) {
                iter._link->_linkCount ++; 
                return;
            }
        }

        if (transit->_destinationLinks->_size >= MAX_DESTINATION_COUNT) {
            ContractedEndpoint* endpoint = EP_transform(transit);
            EP_addIncomingTrack(destination, endpoint, 1);
            FOR_EACH_CONTRACTED_LINK(transit->_destinationLinks) {
                TX_removeDestinatonFromIncomingTrack(transit->_referrer, iter._link->_endpoint);
                EP_addIncomingTrack(iter._link->_endpoint, endpoint, 1);
            }
            destination = endpoint;
        } else {
            ContractedLink ep = { destination, 1};
            Cll_push(transit->_destinationLinks, &ep);
        }
        node = transit->_referrer;
    }
    EP_addIncomingTrack(destination, (ContractedEndpoint*)node, 1);
}

/*
진입 트랙에 목표점 제거.
지정된 추적가능 노드로 부터 역방향으로 경로를 추적하여 다른 축약점 노드(NULL 포함)을 발견할 때 까지
경유점 노드에 목표점을 추가한다. 또한 트랙의 진입점을 발견하면 목표점 노드에 새로운 진입 경로를 추가한다.
*/
void TX_removeDestinatonFromIncomingTrack(TrackableNode* node, ContractedEndpoint* destination) {
    for (TransitNode* transit; (transit = asTransit(node)) != NULL; ) {
        LinkArray* linkArray = transit->_destinationLinks;
        FOR_EACH_CONTRACTED_LINK(linkArray) {
            if (iter._link->_endpoint == destination) {
                if (--iter._link->_linkCount > 0) return;
                Cll_removeFast(linkArray, iter._link);
            }
        }
        node = transit->_referrer;
    }
    EP_removeIncomingTrack(destination, (ContractedEndpoint*)node);
}    



// ===== //

void EP_increaseGroundRefCount(ContractedEndpoint* self, int count) {
    EP_checkType(self);
    assert(count > 0);
    if (self->_groundRefCount == 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
        if (self->_parentCircuit != NULL) {
            self->_parentCircuit->_groundRefCount ++;
        }
    }
    self->_groundRefCount += count;
}

void EP_decreaseGroundRefCount(ContractedEndpoint* self, int count) {
    EP_checkType(self);
    assert(count > 0);
    if ((self->_groundRefCount -= count) == 0 && ENABLE_RT_CIRCULAR_GARBAGE_DETECTION) {
        if (self->_parentCircuit != NULL) {
            self->_parentCircuit->_groundRefCount --;
        }
    }
}


void EP_removeGarbageReferrer(ContractedEndpoint* self, GCObject* referrer) {
    EP_checkType(self);
}

ContractedEndpoint* EP_transform(TransitNode* transit) {
    ContractedEndpoint* self = (ContractedEndpoint*)transit;
    self->_nodeType = Endpoint;
    self->_parentCircuit = NULL;
    self->_incomingLinks = Cll_allocate((GCNode*)self);
    EP_checkType(self);
    return self;
}

void EP_addIncomingTrack(ContractedEndpoint* self, ContractedEndpoint* source, int linkCount) {
    EP_checkType(self);
    EP_checkType(source);
    assert(source != NULL);
    CircuitNode* circuit = self->_parentCircuit;
    if (circuit != NULL && circuit == source->_parentCircuit) {
        circuit->_linkRefCountInCircuit += linkCount;
    }
    FOR_EACH_CONTRACTED_LINK(self->_incomingLinks) {
        if (iter._link->_endpoint == source) {
            iter._link->_linkCount += linkCount;
            return;
        }
    }
    ContractedLink ep = {source, linkCount};
    Cll_push(self->_incomingLinks, &ep);
    RT_detectCircuit(source);
}

void EP_removeIncomingTrack(ContractedEndpoint* self, ContractedEndpoint* source) {
    EP_checkType(self);
    EP_checkType(source);
    assert(source != NULL);
    CircuitNode* circuit = self->_parentCircuit;
    if (circuit != NULL) {
        if (circuit == source->_parentCircuit) {
            circuit->_linkRefCountInCircuit -= 1;
        } else {
            circuit = NULL;
        }
    }
    ContractedLink* link = Cll_pointerOf(self->_incomingLinks, source);
    if (link != NULL) {
        if (--link->_linkCount > 0) return;
        Cll_removeFast(self->_incomingLinks, link);
    }

    if (circuit != NULL) {
        EP_decreaseOutgoingLinkCountInCircuit(source);
    }
}

void EP_decreaseOutgoingLinkCountInCircuit(ContractedEndpoint* self) {
    EP_checkType(self);
    if (--self->_outgoingLinkCountInCircuit > 0) {
        // Circuit should be invalidated !!!
        return;
    }
    
    auto circuit = self->_parentCircuit;
    self->_parentCircuit = NULL;
    FOR_EACH_CONTRACTED_LINK(self->_incomingLinks) {
        if (iter._link->_endpoint->_parentCircuit == circuit) {
            EP_decreaseOutgoingLinkCountInCircuit(iter._link->_endpoint);
        }
    }
}

BOOL EP_isGarbage(ContractedEndpoint* self) {
    EP_checkType(self);
    if (self->_linkRefCount > 0) return 0;
    if (self->_incomingLinks->_size == 0) return 1;
    return self->_parentCircuit != NULL && self->_parentCircuit->_linkRefCount == 0;
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

void RT_onPropertyChanged(PyObject *mp, PyObject *value, PyObject *old_value) {
    printf("RT_onPropertyChanged %p %p -> %p\n", mp, old_value, value);
}

void RT_onDictEntryInserted(PyObject *mp, PyObject *key, PyObject *value) {
    printf("RT_onDictEntryInserted %p[%p] = %p\n", mp, key, value);
}

void RT_onDictEntryRemoved(PyObject *mp, PyObject *key, PyObject *value) {
    printf("RT_onDictEntryRemoved %p[%p] = %p\n", mp, key, value);
}