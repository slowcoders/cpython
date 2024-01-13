#include "GCTest.hpp"
#include "GCNode.hpp"
#include <thread>
#include <random>
#include <unistd.h>
#include <pthread.h>

static int gCntAlloc = 0;
static int gCntDealloc = 0;
static const int _1M = 1024*1024;



struct StressTest {
  
#define GC_TYPE GC_RTGC
  class Node : public GCObject {
    public:
    Node(const char* id) : GCObject() {
#ifdef DEBUG      
        this->_id = id;
#endif        
        gCntAlloc ++;
    }
    
    ~Node() {
        gCntDealloc ++;
    }


    #define PP_CLASS Node
    #include "preprocess/BeginFields.hpp"
    #include "test/TestClass.inl"
    #include "preprocess/EndFields.hpp"
  };


  StressTest() {
  }

  static Node::PTR createCircuit(int extracSpace) {

    // 1050ms
    Node::PTR A1 = new Node("A1");
    Node::PTR A2 = new Node("A2");
    Node::PTR A3 = new Node("A3");
    Node::PTR A4 = new Node("A4");
    Node::PTR A5 = new Node("A5");
    Node::PTR B1 = new Node("B1");
    Node::PTR C1 = new Node("C1");
    Node::PTR C2 = new Node("C2");
    Node::PTR C3 = new Node("C3");
    Node::PTR C4 = new Node("C4");
    Node::PTR C5 = new Node("C5");
    Node::PTR C6 = new Node("C6");
    Node::PTR C7 = new Node("C7");
    Node::PTR D1 = new Node("D1");
    Node::PTR E1 = new Node("E1");
    Node::PTR F1 = new Node("F1");
    Node::PTR G1 = new Node("G1");
    Node::PTR H1 = new Node("H1");
    Node::PTR K1 = new Node("K1");
    Node::PTR K2 = new Node("K2");

    bool makeCircuit = true;
      // 3000ms (without CircuitDetection : 1970ms, for updating referrerList + circuit detection : 2000ms)
    connect(A1, A2);
    connect(A2, A3);
    connect(A3, A4);
    connect(A4, A5);
    if (makeCircuit) connect(A5, A1);

    connect(A2, B1);
    if (makeCircuit) connect(B1, A3);

    connect(A1, C1);
    connect(C1, C2);
    connect(C2, C3);
    connect(C3, C4);
    connect(C4, C5);
    connect(C5, C6);
    connect(C6, C7);
    if (makeCircuit) connect(C7, A4);

    connect(C2, D1);
    if (makeCircuit) connect(D1, C1);

    connect(C2, E1);
    if (makeCircuit) connect(E1, C3);

    connect(E1, F1);
    if (makeCircuit) connect(F1, C4);

    connect(C6, G1);
    if (makeCircuit) connect(G1, C5);

    connect(C7, H1);
    if (makeCircuit) connect(H1, G1);

    connect(F1, K1);
    connect(K1, K2);
    // A1->set_var3(GCPrimitiveArray<char>::alloc(extracSpace));

    return A1;
  }

  static void connect(Node* anchor, Node* link) {
      if (anchor->get_var1() == nullptr) {
          anchor->set_var1(link);
      }
      else if (anchor->get_var2() == nullptr) {
          anchor->set_var2(link);
      }
      else if (anchor->get_var3() == nullptr) {
          anchor->set_var3(link);
      }
      else {
          throw "something wrong";
      }
  }

  static void disconnect(Node* anchor, Node* link) {
      if (anchor->get_var1() == link) {
          anchor->set_var1(nullptr);
      }
      else if (anchor->get_var2() == link) {
          anchor->set_var2(nullptr);
      }
      else if (anchor->get_var3() == link) {
          anchor->set_var3(nullptr);
      }
      else {
          throw "something wrong";
      }
  }

	static void testAllocFreeSpeed(int extraSpace) {
		int TEST_NODE_COUNT = 10*_1M;
		Node** allNodes = (Node**)calloc(sizeof(Node*), TEST_NODE_COUNT);

		SimpleTimer timer;
		timer.reset();
    for (int i = 0; i < TEST_NODE_COUNT; i++) {
      allNodes[i] = new Node("");
    }
    for (int i = 0; i < TEST_NODE_COUNT; i++) {
      delete allNodes[i];
    }
    long init_time = timer.reset();

    for (int i = 0; i < TEST_NODE_COUNT; i++) {
      allNodes[i] = new Node("");
    }
    long alloc_time = timer.reset();

    for (int i = 0; i < TEST_NODE_COUNT; i++) {
      delete allNodes[i];
    }
    int gc_time = timer.reset();
    free(allNodes);

		printf("-------------\n");
		printf("Memory manager test %dM nodes.\n", TEST_NODE_COUNT/_1M);
		//printf(" - Test initialization: %d ms\n", init_time);
		printf(" - Allocation: %ld ms\n", alloc_time);
		printf(" - Deletion: %d ms\n", gc_time);
		printf(" - Total elaspsed: %ld ms\n", (alloc_time + gc_time));
		printf("-------------\n\n");
	}
};

#undef PP_CLASS
#define PP_CLASS StressTest::Node
#include "preprocess/BeginScanInfo.hpp"
  #include "test/TestClass.inl"
#include "preprocess/EndScanInfo.hpp"
#undef PP_CLASS

struct MemBucket {
  MemBucket* next;
  char dummy[4096-sizeof(void*)];
};
void checkMaxMemory() {
  int idx = 0;
  MemBucket* root = nullptr;
  for (;; idx++) {
    MemBucket* mem = (MemBucket*)malloc(sizeof(MemBucket));
    if (mem == nullptr) break;
    if ((idx % 1000) == 0) {
      printf("mem size: %luM\n", idx * sizeof(MemBucket) / _1M);
    }
    mem->next = root;
    root = mem;
  }
  for (; root != nullptr;) {
    MemBucket* mem = root;
    root = mem->next;
    free(mem);
  }
  printf("maxMemory: %dM\n", idx);

}

void GCObject::getReferents(GCObject* obj, std::vector<GCObject*>& referents) {
    StressTest::Node* anchor = (StressTest::Node*)obj;
    if (anchor->get_var1() != NULL) {
        referents.push_back(anchor->get_var1());
    }
    if (anchor->get_var2() != NULL) {
        referents.push_back(anchor->get_var2());
    }
    if (anchor->get_var3() != NULL) {
        referents.push_back(anchor->get_var3());
    }
}




int main(int argc, const char* args[]) {
  int increment = argc > 2 ? atoi(args[1]) : 1000;
  int extraSpace = argc > 2 ? atoi(args[2]) : 1000;
  // checkMaxMemory();
  // StressTest::testAllocFreeSpeed(extraSpace);

  int cntCircuit = 0; 
  for (int i = 0; i < 8; i ++) {
    cntCircuit += increment;
    StressTest::createCircuit(cntCircuit);
  }
}

