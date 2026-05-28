### branch
[3.14] gh-148487: Fix issues in `test_add_python_opts` (GH-148507) (#148545)
git clone --depth=1024 https://SECRET@github.com/slowcoders/cpython.git
git remote add upstream https://github.com/python/cpython.git
git fetch upstream master --depth 500


### build distrbution version of Mac
see https://github.com/python/cpython/blob/main/Mac/README.rst [How do I create a binary distribution?]

### configure and build
#### for Debug
```sh
mkdir _debug
cd _debug
../configure --with-pydebug 
make
make test
## test 실행.
./python.exe -m test _test_multiprocessing.py
```

#### for Release (--with-lto: link time optimization)
```sh
mkdir _release
cd _release
../configure --enable-optimizations --with-lto
make
make test
```

### Makefile 변경 필요시, Makfile.pre.in 을 변경한다.

### testing 
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

### gcmodule.c
PyGC_Head { _gc_next, __gc_prev };
PyType_GenericAlloc
    _PyObject_GC_TRACK (<-> _PyObject_GC_UNTRACK)
        _Py_TriggerGC
            _Py_ScheduleGC
                _Py_set_eval_breaker_bit(tstate, _PY_GC_SCHEDULED_BIT);

    _Py_HandlePending
        _Py_RunGC
            _PyGC_Collect

_Py_RunGC
    _PyGC_Collect
        gc_collect_increment/young/full
            gc_collect_region
                deduce_unreachable


### gc_collect_young
1. gc_collect_region(...)
2. deduce_unreachable
    2.1 모든 Young 객체에 대해
        gc_reset_refs(gc, Py_REFCNT(op));
        {
            g->_gc_prev = (g->_gc_prev & _PyGC_PREV_MASK_FINALIZED)
                | PREV_MASK_COLLECTING
                | ((uintptr_t)(refs) << _PyGC_PREV_SHIFT);
        }
    2.2 모든 Young 객체에 대해
        gc_decref(PyGC_Head *g)
        {
            _PyObject_ASSERT_WITH_MSG(FROM_GC(g),
                                    gc_get_refs(g) > 0,
                                    "refcount is too small");
            g->_gc_prev -= 1 << _PyGC_PREV_SHIFT;
        }



### pylifecycle.c
_PyRuntimeState _PyRuntime;
_PyThreadState_GET(void)
{
    return _PyRuntimeState_GetThreadState(&_PyRuntime);
}
_PyRuntimeState_GetThreadState(_PyRuntimeState *runtime)
{
    // 한 쓰레드가 interpreter 를 locking (gil) 하여 interpreter 를 독점.
    return (PyThreadState*)_Py_atomic_load_relaxed(&runtime->gilstate.tstate_current);
}

### Py_XSETREF, Py_SETREF
변수 변경 후, Py_DECREF/Py_XDECREF 호출

### Py_INCREF/Py_XINCREF, Py_DECREF/Py_XDECREF, Py_SET_REFCNT (_Py_IncRef, _Py_DecRef)
stack 변수 처리용? 주로 c lib 함수에 사용됨.

### _Py_DECREF_INT, _Py_DECREF_SPECIALIZED, _Py_DECREF_NO_DEALLOC
주로 ceval.c 에서 사용. primitive 또는 stack 변수 처리 시 사용.

### (No-RTGC)
10 slowest tests:
- test_subprocess: 1 min 16 sec
- test_signal: 41.7 sec
- test_io: 32.9 sec
- test.test_multiprocessing_spawn.test_processes: 32.8 sec
- test.test_multiprocessing_forkserver.test_processes: 31.0 sec
- test.test_concurrent_futures.test_process_pool: 29.4 sec
- test_socket: 28.4 sec
- test.test_multiprocessing_forkserver.test_misc: 28.0 sec
- test_urllib2net: 27.7 sec
- test_xmlrpc: 27.3 sec

32 tests skipped:
    test.test_asyncio.test_windows_events
    test.test_asyncio.test_windows_utils test.test_gdb.test_backtrace
    test.test_gdb.test_cfunction test.test_gdb.test_cfunction_full
    test.test_gdb.test_misc test.test_gdb.test_pretty_print
    test.test_multiprocessing_fork.test_manager
    test.test_multiprocessing_fork.test_misc
    test.test_multiprocessing_fork.test_processes
    test.test_multiprocessing_fork.test_threads test_android
    test_dbm_gnu test_devpoll test_epoll test_free_threading test_idle
    test_launcher test_msvcrt test_perf_profiler test_perfmaps
    test_startfile test_tcl test_tkinter test_ttk test_ttk_textonly
    test_turtle test_winapi test_winconsoleio test_winreg
    test_winsound test_wmi

3 tests skipped (resource denied):
    test_peg_generator test_xpickle test_zipfile64

457 tests OK.

Total duration: 2 min 19 sec
Total tests: run=47,011 skipped=2,118
Total test files: run=489/492 skipped=32 resource_denied=3