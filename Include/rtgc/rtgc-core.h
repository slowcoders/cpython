#ifndef RTGC_CORE_H_
#define RTGC_CORE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define rt_assert_f(p, ...)                                 \
do {                                                        \
  if (!(p)) {                                               \
    printf("%s:%d assert fail(" #p ") ", __FILE__, __LINE__);\
    printf(__VA_ARGS__);                                    \
    RT_break();                                       \
  }                                                         \
} while (0)

#define RTGC_DEBUG  1
#if RTGC_DEBUG
#define rt_log(...)         printf(__VA_ARGS__)
#define rt_log_v(...)       if (RTGC_LOG_VERBOSE) printf(__VA_ARGS__)
#define rt_log_if(p, ...)   if (p) printf(__VA_ARGS__)
#else 
#define rt_log(...)
#define rt_log_v(...)
#define rt_log_if(p, ...)
#endif

#if INCLUDE_RTGC
#define RTGC_ONLY(...)      __VA_ARGS__
#define NOT_RTGC(...)       
#define rt_assert(p, ...)   rt_assert_f(p, "" __VA_ARGS__)
#else
#define RTGC_ONLY(...)      
#define NOT_RTGC(...)       __VA_ARGS__
#define rt_assert(p, ...)   
#endif

extern int RTGC_ENABLE;
extern int RTGC_LOG_VERBOSE;

typedef int BOOL;

// #define RTGC_HEAD_EXTRA           

#define RTGC_HEAD_EXTRA           \
    struct _object* _anchor;      \
    struct _RCircuit* _circuit;   

#define RTGC_EXTRA_INIT   0, 0,

void RT_onPropertyChanged(PyObject *obj, PyObject *old_value, PyObject *value);
void RT_replaceReferrer(PyObject *obj, PyObject *old_referrer, PyObject *referrer);
void RT_onDestoryGarbageNode(PyObject *obj, PyTypeObject *type);
//void RT_onDictEntryInserted(PyObject *obj, PyObject *key, PyObject *value);
//void RT_onDictEntryRemoved(PyObject *obj, PyObject *key, PyObject *value);

PyAPI_FUNC(void) RT_onIncreaseRefCount(PyObject *obj);
PyAPI_FUNC(BOOL) RT_onDecreaseRefCount(PyObject *obj);

void RT_break(void);

#ifdef __cplusplus
}
#endif
#endif  // RTGC_CORE_H_