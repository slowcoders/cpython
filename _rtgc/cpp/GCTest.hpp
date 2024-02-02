#include "GCNode.hpp"
#include <memory.h>


#define GC_Trace  	0
#define GC_RC		1
#define GC_RTGC		2
class GCObject;

template <class T, int gc_type>
class RC_PTR {
protected:	
	T* _ptr;
public:
	RC_PTR() { this->_ptr = nullptr; }

	RC_PTR(const RC_PTR& p) : RC_PTR(p._ptr) {}

	RC_PTR(T* const ptr) { 
		this->_ptr = ptr; 
		increase_RC((GCObject*)ptr);
	}

	~RC_PTR() {
		decrease_RC((GCObject*)_ptr);
	}

	void operator=(const RC_PTR& p) {
		this->operator=(p._ptr);
	}

	void operator=(T * ptr) {
		T* old = this->_ptr;
		if (old == ptr) {
			return;
		}
		this->_ptr = ptr;
		GarbageCollector::processRootVariableChange((GCObject*)ptr, (GCObject*)old);
	}

	T* operator -> () {
		return _ptr;
	}

	operator T* () {
		return _ptr;
	}

	bool operator == (T* ptr) {
		return _ptr == ptr;
	}

	bool operator != (T* ptr) {
		return _ptr != ptr;
	}

	static void increase_RC(GCObject* obj) {
		if (obj == nullptr) return;
		GarbageCollector::processRootVariableChange(obj, NULL);
	}

	static void decrease_RC(GCObject* obj) {
		if (obj == nullptr) return;
		GarbageCollector::processRootVariableChange(NULL, obj);
		// if (obj->_nodeisUnreachable()) {
		// 	delete (obj);
		// }
	}

	T* getRawPointer_unsafe() const {
		return _ptr;
	}
};


class GCTest {
	GCObject* _root;
	int _cntAllocated;
	int _cntDeleted;
	int _gcType;
public:	
	void init(GCObject* root, int gcType);
	void onAllocated(GCObject* obj);
	void onDeleted(GCObject* obj);
	void executeGC();
};


