static const int MIN_CAPACITY = 8;
/* 축약 연결 정보 */
struct _ContractedLink {
    struct _ContractedEndpoint* _endpoint;
    int _linkCount;
};

static inline void initContractedLink(ContractedLink* self, ContractedEndpoint* endpoint, int count) {
    self->_endpoint = endpoint;
    self->_linkCount = count;
}



typedef struct _ContractedLink _Item;

typedef struct _LinkArray {
    int _size;
    int _capacity;
    GCNode* _owner;
    _Item* _items;
} LinkArray;

typedef struct {
    _Item* _link;
    _Item* _end;
} LinkIterator;

#define FOR_EACH_CONTRACTED_LINK(links)    \
    for ( LinkIterator iter={links->_items, links->_items + links->_size}; iter._link < iter._end; iter._link++)


static inline void Cll_allocItems(LinkArray* array, int size) {
    int mask = MIN_CAPACITY - 1;
    int capacity = (size + mask) & ~mask; 
    int memsize = sizeof(_Item) * capacity;
    _Item* items = array->_items;
    array->_items = (_Item*)((items == NULL) ? PyMem_Malloc(memsize) : PyMem_Realloc(items, memsize));
    array->_capacity = capacity;
}

LinkArray* Cll_allocate(GCNode* node) {
    LinkArray* array = (LinkArray*)PyMem_Malloc(sizeof(LinkArray));
    array->_items = NULL;
    array->_size = 0;
    array->_owner = node;
    Cll_allocItems(array, 1);
    return array;
}

int Cll_size(LinkArray* array) {
    return array == NULL ? 0 : array->_size;
}

BOOL Cll_isEmpty(LinkArray* array) {
    return array == NULL || array->_size == 0;
}

_Item* Cll_pointerOf(LinkArray* array, ContractedEndpoint* ep) {
    int idx = Cll_size(array);
    for (_Item* pItem = array->_items; --idx >= 0; pItem++) {
        if (pItem->_endpoint == ep) return pItem;
    }
    return NULL;
}

void Cll_push(LinkArray* array, _Item* item) {
    int size = array->_size;
    if (size >= array->_capacity) {
        Cll_allocItems(array, size + 1); 
    }
    array->_items[array->_size ++] = *item;
}

void Cll_removeFast(LinkArray* array, _Item* pItem) {
    assert(pItem >= array->_items && pItem < array->_items + array->_capacity);
    int newSize = --array->_size;
    assert(newSize >= 0);
    *pItem = array->_items[newSize];
    if (newSize < (array->_capacity - MIN_CAPACITY) / 2) {
        Cll_allocItems(array, newSize);
    }
}

void Cll_delete(LinkArray* array) {
    PyMem_Free(array->_items);
    PyMem_Free(array);
}

LinkArray* Cll_clone(LinkArray* src, GCNode* node) {
    LinkArray* array = (LinkArray*)PyMem_Malloc(sizeof(LinkArray));
    array->_items = NULL;
    array->_size = src->_size;
    array->_owner = node;
    Cll_allocItems(array, src->_size);
    memcpy(array->_items, src->_items, sizeof(_Item) * src->_size);
    return array;
}