const MAX_DESTINATION_COUNT = 4;

export class GCObject {
    _node: RefNode;
}

export class FieldUpdateInfo {
    owner: GCObject;
    assigned: GCObject;
    erased: GCObject;
}

class ContractedLink {
    _endpoint: ContractedNode;
    _count: number;

    constructor(endpoint: ContractedNode, count: number) {
        if (!endpoint) throw new Error('invalid endpoint');
        this._endpoint = endpoint;
        this._count = count;
    }

    toString() {
        return this._endpoint.toString();
    }
}


export abstract class RefNode {
    _obj: GCObject;
    _linkRefCount: number;
    
    constructor(obj: GCObject) {
        this._obj = obj;
        this._linkRefCount = 0;
        if (obj != null) obj._node = this;
    }

    isContractedEndpoint() { return false; }
    markDestroyed() { this._obj = null; }
    isDestroyed() { return this._obj == null; }

    abstract isGarbage(): boolean;
    abstract addReferrer(referrer: GCObject): void;
    abstract removeReferrer(referrer: GCObject): void;
    abstract increaseGroundRefCount(): void;
    abstract decreaseGroundRefCount(amount: number): void;

    abstract getCyclicContainer(): CircuitNode;

    toString() {
        return this._obj?.toString();
    }
}


export class GCRuntime {
    static onObjectCreated(obj: GCObject, isAcyclic: boolean) {
        new TransitNode(obj);
    }

    static processFieldUpdate(updateInfo: FieldUpdateInfo) {
        if (updateInfo.assigned === updateInfo.erased) return;

        if (updateInfo.assigned != null && updateInfo.assigned !== updateInfo.owner) {
            updateInfo.assigned._node.addReferrer(updateInfo.owner);
        }
        if (updateInfo.erased != null && updateInfo.erased !== updateInfo.owner) {
            updateInfo.erased._node.removeReferrer(updateInfo.owner);
            if (updateInfo.erased._node.isGarbage()) {
                GarbageScanner.collectGarbage(updateInfo.erased._node);
            }
        }
    }

    static increaseGroundRefCount(obj: GCObject) {
        obj._node.increaseGroundRefCount();
    }

    static decreaseGroundRefCount(obj: GCObject) {
        obj._node.decreaseGroundRefCount(1);
        if (obj._node.isGarbage()) {
            GarbageScanner.collectGarbage(obj._node);
        }
    }
};


export class TransitNode extends RefNode {
    _referrer: GCObject | null;
    _signposts: ContractedLink[];

    constructor(obj: GCObject) {
        super(obj);
        this._referrer = null;
        this._signposts = [];
    }

    increaseGroundRefCount() {
        if (this._linkRefCount ++ == 0) {
            for (const link of this._signposts) {
                link._endpoint.increaseGroundRefCount();
            }
        }
    }

    decreaseGroundRefCount(amount: number) {
        if ((this._linkRefCount -= amount) == 0) {
            for (const link of this._signposts) {
                link._endpoint.decreaseGroundRefCount(1);
            }
        }
    }

    addReferrer(newReferrer: GCObject) {
        const oldReferrer = this._referrer;
        if (oldReferrer == null) {
            this._referrer = newReferrer;
            if (this._signposts.length > 0) {
                const oldOrigin = null;
                for (const link of this._signposts) {
                    link._endpoint.removeContractedIncomingLink(oldOrigin);
                    ContractedGraph.addOutgoingContractedLink(newReferrer, link._endpoint);
                }
            } else {
                this.detectCyclicNodeInNotContractedPath();
            }
        } else {
            const newNode = new ContractedNode(this._obj);
            for (const link of this._signposts) {
                ContractedGraph.removeContractedOutgoingLink(oldReferrer, link._endpoint);
            }
            ContractedGraph.addOutgoingContractedLink(oldReferrer, newNode);
            for (const link of this._signposts) {
                // newReferrer.addOutgoingContractedLink() 전에 decreaseGroundRefCount 처리.
                link._endpoint.decreaseGroundRefCount(newNode._linkRefCount);
                link._endpoint.addIncomingContractedLink(newNode, link._count);
            }
            ContractedGraph.addOutgoingContractedLink(newReferrer, newNode);
        }
    }

    removeReferrer(referrer: GCObject) {
        this._referrer = null;
        for (const link of this._signposts) {
            ContractedGraph.removeContractedOutgoingLink(referrer, link._endpoint);
        }
    }

    isGarbage() {
        return this._linkRefCount == 0 && this._referrer == null;
    }

    getOriginOfContractedPath() {
        let node = this._referrer?._node;
        while (node != null && !node.isContractedEndpoint()) {
            node = (<TransitNode>node)._referrer?._node;
        }
        return <ContractedNode>node;
    }

    getCyclicContainer() {
        const originNode = this.getOriginOfContractedPath();
        if (originNode?._parentCircuit != null) {
            for (const link of this._signposts) {
                if (link._endpoint._parentCircuit == originNode._parentCircuit) {
                    return originNode._parentCircuit;
                }
            }
        }
        return null;
    }

    detectCyclicNodeInNotContractedPath() {
        if (this._signposts.length > 0) return;
        let obj = this._referrer;
        while (obj != null && !obj._node.isContractedEndpoint()) {
            const transit = <TransitNode>obj._node;
            if (transit._signposts.length > 0) break; 
            obj = transit._referrer;
            if (transit == this) {
                const endpoint = new ContractedNode(this._obj);            
                ContractedGraph.addOutgoingContractedLink(this._referrer, endpoint);
                return;
            }
        }
    }
}

export class ContractedNode extends RefNode {
    _parentCircuit: CircuitNode | null; 
    _incomingLinks: ContractedLink[];
    _outgoingLinkCountInCircuit: number;

    constructor(obj: GCObject) {
        super(obj);
        this._parentCircuit = null;
        this._incomingLinks = [];
    }

    isContractedEndpoint() { return true; }

    getCyclicContainer() {
        return this._parentCircuit;
    }

    increaseGroundRefCount() {
        if (this._linkRefCount ++ == 0) {
            if (this._parentCircuit != null) {
                this._parentCircuit._linkRefCount ++;
            }
        }
    }

    decreaseGroundRefCount(delta: number) {
        if ((this._linkRefCount -= delta) == 0) {
            if (this._parentCircuit != null) {
                this._parentCircuit._linkRefCount --;
            }
        }
    }

    addReferrer(referrer: GCObject) {
        ContractedGraph.addOutgoingContractedLink(referrer, this);
    }

    removeReferrer(referrer: GCObject) {
        ContractedGraph.removeContractedOutgoingLink(referrer, this);
    }

    addIncomingContractedLink(endpoint: ContractedNode, linkCount: number) {
        if (endpoint == null) {
            if (this._linkRefCount ++ > 0) return;
        } else {
            const conn = this._incomingLinks.find((conn) => conn._endpoint === endpoint);
            if (conn != null) {
                conn._count += linkCount;
                return;
            }
            this._incomingLinks.push(new ContractedLink(endpoint, linkCount))
            new CircuitDetector().checkCyclic(this);
        }

        if (this._parentCircuit != null && endpoint?._parentCircuit != this._parentCircuit) {
            this._parentCircuit._linkRefCount ++;
        }                
    }

    removeContractedIncomingLink(endpoint: ContractedNode) {
        if (endpoint == null) {
            if (--this._linkRefCount > 0) return;
        }
        else {
            const idx = this._incomingLinks.findIndex((conn) => conn._endpoint === endpoint);
            if (idx < 0) {
                console.log(endpoint);
            }
            const conn = this._incomingLinks[idx];
            if (--conn._count > 0 || this._incomingLinks.splice(idx, 1).length > 0) return;
        }

        if (this._parentCircuit != null) {
            if (endpoint?._parentCircuit == this._parentCircuit) {
                endpoint.decreaseOutgoingLinkCountInCircuit();
            } else {
                this._parentCircuit._linkRefCount --;
            }
        }
    }

    decreaseOutgoingLinkCountInCircuit() {
        if (--this._outgoingLinkCountInCircuit > 0) return;
        
        const circuit = this._parentCircuit;
        this._parentCircuit = null;
        for (const link of this._incomingLinks) {
            if (link._endpoint._parentCircuit == circuit) {
                link._endpoint.decreaseOutgoingLinkCountInCircuit();
            }
        }
    }

    isGarbage() {
        if (this._linkRefCount > 0) return false;
        if (this._incomingLinks.length == 0) return true;
        return this._parentCircuit != null && this._parentCircuit._linkRefCount == 0;
    }
}

export class CircuitNode extends ContractedNode {
    constructor() {
        super(null);
    };
}

class CircuitDetector {
    visitedNodes: ContractedNode[] = [];
    traceStack: ContractedNode[] = []
    circularNodes: ContractedNode[] = []
    circuitInStack = null;

    checkCyclic(endpoint: ContractedNode) {
        if (this.visitedNodes.indexOf(endpoint) >= 0) return;

        const idx = this.traceStack.indexOf(endpoint);
        if (idx >= 0) {
            let circuit = this.circuitInStack;
            if (circuit == null) {
                circuit = this.circuitInStack = new CircuitNode();
            }
            for (let i = this.traceStack.length; --i >= idx;) {
                const node = this.traceStack[i];
                if (node._parentCircuit == null) {
                    node._parentCircuit = circuit;
                    if (node._linkRefCount > 0) {
                        circuit._linkRefCount ++;
                    }
                }
            }
            return;
        }

        const stackDepth = this.traceStack.length;
        this.traceStack.push(endpoint);
        endpoint._parentCircuit = null;
        endpoint._outgoingLinkCountInCircuit = 0;
        let externalLinkCount = 0;
        for (const link of endpoint._incomingLinks) {
            this.checkCyclic(link._endpoint);
            const circuit = link._endpoint._parentCircuit;
            if (circuit != null && circuit == endpoint._parentCircuit) {
                link._endpoint._outgoingLinkCountInCircuit ++;
            } else {
                externalLinkCount ++;
            }
        }
        if (endpoint._parentCircuit != null) {
            endpoint._parentCircuit._linkRefCount += externalLinkCount;
        } else {
            this.traceStack.length = stackDepth;
            this.circuitInStack = null;
        }
        this.visitedNodes.push(endpoint);
    }
};


class GarbageScanner {
    static collectGarbage(node: RefNode) {
        const obj = node._obj;
        node.markDestroyed();
        for (const field in obj) {
            const node = obj[field]?._node;
            if (node == null || node.isDestroyed()) continue;
            if (node.isContractedEndpoint()) {
                // garbage 판별이 이미 완료됨.
                if (node.isGarbage()) {
                    this.collectGarbage(node);
                }
            }
            else {
                if (node._linkRefCount > 0) {
                    node._referrer = null;
                } else {
                    this.collectGarbage(node)
                }
            }
        }
    }
}

class ContractedGraph {
    static addOutgoingContractedLink(obj: GCObject, endpoint: ContractedNode) {
        let node: RefNode = obj._node;
        while (node != null && !node.isContractedEndpoint()) {
            const transit = <TransitNode>node;
            const link = transit._signposts.find((link) => link._endpoint == endpoint);
            if (link != null) {
                link._count ++; 
                return;
            }
            if (transit._signposts.length >= MAX_DESTINATION_COUNT) {
                const new_node = new ContractedNode(transit._obj);
                endpoint.addIncomingContractedLink(new_node, 1);
                for (const link of transit._signposts) {
                    this.removeContractedOutgoingLink(transit._referrer, link._endpoint)
                    link._endpoint.addIncomingContractedLink(new_node, 1);
                }
                endpoint = new_node;
            } else {
                transit._signposts.push(new ContractedLink(endpoint, 1));
            }
            node = transit._referrer?._node;
        }
        endpoint.addIncomingContractedLink(<ContractedNode>node, 1);
    }

    static removeContractedOutgoingLink(obj: GCObject, endpoint: ContractedNode) {
        let node: RefNode = obj._node;
        while (node != null && !node.isContractedEndpoint()) {
            const transit = <TransitNode>node;
            for (let i = transit._signposts.length; --i >= 0;) {
                const link = transit._signposts[i];
                if (link._endpoint == endpoint) {
                    if (--link._count == 0) {
                        transit._signposts.splice(i, 1);
                    }
                    return;
                }
            }
            node = transit._referrer?._node;
        }
        endpoint.removeContractedIncomingLink(<ContractedNode>node);
    }
}
