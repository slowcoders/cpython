#define PP_REPLACE_FIELD(field, v) replaceMemberVariable(this, (GCObject**)(void*)&this->field, v)
    public: typedef RC_PTR<PP_CLASS, GC_TYPE>    PTR;

static uint16_t _fieldMap[];
uint16_t* getFieldOffsets() {
    return _fieldMap;
}

PP_CLASS() : GCObject() {}

#define PP_FIELD_OBJ(TYPE, NAME) \
    private: TYPE* NAME;  \
    public: TYPE::PTR get_##NAME() { return NAME; }  \
    public: void set_##NAME(TYPE* v) { PP_REPLACE_FIELD(NAME, v); }


