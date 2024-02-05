#ifndef RTGC_CORE_H_
#define RTGC_CORE_H_

#define rt_assert_f(p, ...)                                 \
do {                                                        \
  if (!(p)) {                                               \
    printf("%s:%d assert(" #p ") ", __FILE__, __LINE__);    \
    printf(__VA_ARGS__);                                    \
  }                                                         \
} while (0)

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

void RT_onPropertyChanged(PyObject *obj, PyObject *old_value, PyObject *value);
void RT_onDictEntryInserted(PyObject *obj, PyObject *key, PyObject *value);
void RT_onDictEntryRemoved(PyObject *obj, PyObject *key, PyObject *value);
void RT_replaceReferrer(PyObject *obj, PyObject *old_referrer, PyObject *referrer);

#endif  // RTGC_CORE_H_