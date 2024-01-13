#include <stdlib.h>

#include <deque>
#include <list>
#include <map>
#include <vector>
#include <functional>
#include <chrono>

#ifdef DEBUG
  #include "assert.h"
#elif !defined(assert)
  #define assert(t) // ignore
#endif

#define PP_MERGE_TOKEN_EX(L, R)	L##R
#define PP_MERGE_TOKEN(L, R)	PP_MERGE_TOKEN_EX(L, R)

#define PP_TO_STRING_EX(T)	    #T
#define PP_TO_STRING(T)			PP_TO_STRING_EX(T)

#define NO_INLINE __attribute__((noinline))

template<class Value>
using KStdDeque = std::deque<Value>;

template <class T>
class PointerList {
  T* _items;
  int _size;
  int _maxSize;
public:

  PointerList(size_t maxSize = 8) {
    _items = (T*)malloc(sizeof(T) * maxSize);
    _maxSize = maxSize;
    _size = 0;
  }
  
  ~PointerList() {
    free(_items);
  }
  
  int size() {
    return _size;
  }
  
  void push_back(T item) {
    if (_size >= _maxSize) {
      throw "something wrong";
    }
    _items[_size ++] = item;
  }
  
  T& push_space() {
    return _items[_size++];
  }

  T& operator[](size_t __n)
  {
    return _items[__n];
  }
  
  T& at(size_t __n)
  {
    return _items[__n];
  }
  
  void resize(size_t __n) {
    _size = __n;
  }
  
  int indexOf(T v) {
    T* pObj = _items + _size;
    for (int i = (int)_size; --i >= 0; ) {
      --pObj;
      if (*pObj == v) {
        return i;
      }
    }
    return -1;
  }
  
  PointerList* operator -> () {
    return this;
  }
  
  bool remove(T v) {
    int idx = indexOf(v);
    if (idx < 0) {
      return false;
    }
    removeFast(idx);
    return true;
  }

  void removeFast(int idx) {
    assert(idx >= 0 && idx < this->size());
    int newSize = this->size() - 1;
    if (idx < newSize) {
      _items[idx] = this->at(newSize);
    }
    this->resize(newSize);
  }

};

/*
template<class Value>
struct KStdVector : public std::vector<Value> {
    intptr_t external_member_ref_cout;
    bool remove(Value v) {
        for(auto it = std::begin(*this); it != std::end(*this); ++it) {
            if (*it == v) {
                this->erase(it);
                return true;
            }
        }
        return false;
    }

    void removeFast(int idx) {
        assert(idx >= 0 && idx < this->size());
        int newSize = this->size() - 1;
        if (idx < newSize) {
            this->at(idx) = this->at(newSize);
        }
        this->resize(newSize);
    }

    void insertAt(int idx, Value v) {
        this->insert(this->begin() + idx, v);
    }

    KStdVector* operator -> () {
        return this;
    }

    int indexOf(Value v) {
        for (int i = this->size(); --i >= 0; ) {
            if (this->at(i) == v) {
                return i;
            }
        }
        return -1;
    }

};
 */

class SimpleTimer {
    int64_t t1;
    int64_t t2;
public:
    SimpleTimer() { 
      reset(); 
      t1 = t2; 
    }

    int64_t reset() {
        t1 = t2;
        t2 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return t2 - t1;
    }
};
