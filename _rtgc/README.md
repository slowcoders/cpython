### build distrbution version of Mac
see https://github.com/python/cpython/blob/main/Mac/README.rst [How do I create a binary distribution?]

### configure and build
for Debug
```sh
mkdir _debug
cd _debug
../configure --with-pydebug --with-lto
make
make test
```

for Release
```sh
mkdir _release
cd _release
../configure --enable-optimizations --with-lto
make
make test
```

retest
```hh
make test TESTOPTS="-v test_os test_gdb"
```

```c
PyList_Append(PyObject *op, PyObject *newitem) {
    Py_INCREF(newitem);
    return _PyList_AppendTakeRef((PyListObject *)op, newitem) {
        PyList_SET_ITEM(self, len, newitem) {
            PyListObject *list = _PyList_CAST(op);
            list->ob_item[index] = value;
        }
    }
}
```

- Cell 객체
Reference ???

### Py_XSETREF, Py_SETREF
변수 변경 후, Py_DECREF/Py_XDECREF 호출

### Py_INCREF/Py_XINCREF, Py_DECREF/Py_XDECREF, Py_SET_REFCNT (_Py_IncRef, _Py_DecRef)
stack 변수 처리용? 주로 c lib 함수에 사용됨.

### _Py_DECREF_INT, _Py_DECREF_SPECIALIZED, _Py_DECREF_NO_DEALLOC
주로 ceval.c 에서 사용. primitive 또는 stack 변수 처리 시 사용.

