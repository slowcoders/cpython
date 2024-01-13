static const int MIN_CAPACITY = 8;
/* 축약 연결 정보 */
struct _ContractedLink {
    struct _ContractedEndpoint* _endpoint;
    int _linkCount;
};

typedef struct _ContractedLink _Item;

typedef struct _LinkArray {
    int _size;
    int _capacity;
    _Item* _items;
} LinkArray;

#define FOR_EACH_DESTINATION_LINKS(links)    \
    for (_Item* link=links->_items + links->size; --link >= links->_items; )


inline void LinkArray_allocItems(LinkArray* array, int size) {
    int mask = MIN_CAPACITY - 1;
    int capacity = (size + mask) & ~mask; 
    int memsize = sizeof(_Item) * capacity;
    _Item* items = array->_items;
    items = (_Item*)((items == NULL) ? PyMem_Malloc(memsize) : PyMem_Realloc(items, memsize));
    array->_capacity = capacity;
}

LinkArray* LinkArray_allocate() {
    LinkArray* array = (LinkArray*)PyMem_Malloc(sizeof(LinkArray));
    array->_items = NULL;
    array->_size = 0;
    LinkArray_allocItems(array, 1);
    return array;
}

int LinkArray_size(LinkArray* array) {
    return array->_size;
}

_Item* LinkArray_pointerOf(LinkArray* array, ContractedEndpoint* point) {
    int idx = LinkArray_size(array);
    for (_Item* pItem = array->_items; --idx >= 0; pItem++) {
        if (*pItem == item) return pItem;
    }
    return NULL;
}

void LinkArray_push(LinkArray* array, _Item item) {
    int size = array->_size;
    if (size >= array->_capacity) {
        LinkArray_allocItems(array, size + 1); 
    }
    array->_items[array->_size ++] = item;
}

void LinkArray_removeFast(LinkArray* array, _Item* pItem) {
    assert(pItem >= array->_items && pItem < array->_items + array->_capacity);
    int newSize = --array->_size;
    assert(newSize >= 0);
    *pItem = array->_items[newSize];
    if (newSize < (array->_capacity - MIN_CAPACITY) / 2) {
        LinkArray_allocItems(array, newSize);
    }
}

void LinkArray_delete(LinkArray* array) {
    PyMem_Free(array->_items);
    PyMem_Free(array);
}

