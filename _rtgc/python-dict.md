## AbstractDict
- tp_dictoffset != 0 인 type
또는
- (tp_flags & Py_TPFLAGS_DICT_SUBCLASS) != 0 인 PyDict_Type 및 그 하위 CLASS 

### (tp_flags & Py_TPFLAGS_MANAGED_DICT) != 0
- obj.__dict__ 로 접근할 수 있는 AbstractDict 타입.
- tp_dictoffset > 0 이다.

static PyGetSetDef func_getsetlist[] = {<br>
    {"__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict},<br>
};


- tp_dictoffset != 0;
offsetof.*tp_dictoffset 