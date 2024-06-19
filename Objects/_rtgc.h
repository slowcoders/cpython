#ifndef GCOBJECT_HPP
#define GCOBJECT_HPP

#include "Python.h"
#include <stdlib.h>
#include <assert.h>

enum GCNodeType { Acyclic, Transit, Endpoint, Circuit, Destroyed = -1 };

static const BOOL true = 1;
static const BOOL false = 0;

typedef struct _object GCObject;
typedef struct _object GCNode;
typedef struct _RCircuit RCircuit;

static const int MAX_DESTINATION_COUNT = 4; 
static const int ENABLE_RT_CIRCULAR_GARBAGE_DETECTION = 1;

#define RTNode_Header()             \
    int32_t _groundRefCount;        \
    enum GCNodeType _nodeType;      \

static const int IS_DIRTY_ANCHOR = 1;
static const int HAS_DIRTY_ANCHORS = 2;
static const int DIRTY_ANCHOR_FLAGS = IS_DIRTY_ANCHOR | HAS_DIRTY_ANCHORS;
/*
GarbageCollector
*/
struct _RCircuit {
    Py_ssize_t ob_refcnt;
    Py_ssize_t _internalRefCount;
};

#define NUM_ARGS(...)  (sizeof((int[]){0, ##__VA_ARGS__})/sizeof(int)-1)

#define RT_INVOKE_V(method, self) \
    _rt_vtables[self->_nodeType].method(self)

#define RT_INVOKE(method, self, ...) \
    _rt_vtables[self->_nodeType].method(self, __VA_ARGS__)


static inline GCNode* RT_getGCNode(GCObject* obj)  { return (GCNode*)obj; }
static inline GCObject* RT_getObject(GCNode* node) { return (GCObject*)node; }


#endif  // GCOBJECT_HPP
