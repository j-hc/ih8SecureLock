#pragma once

#include <stdint.h>

#if defined(__LP64__)
#define FLAT_BINDER_OBJ_SIZE 24
#else
#define FLAT_BINDER_OBJ_SIZE 16
#endif

#define STUB(n) (n "$Stub")
#define TRSCTN(n) ("TRANSACTION_" n)

struct PParcel {
    size_t error;
    char* data;
    size_t data_size;
};

struct FakeParcel {
   private:
    char* data;
    uint32_t cur;

   public:
    FakeParcel(char* data);

    void skip(uint32_t n);
    uint32_t getCursor();
    void skipFlatObj();

    uint32_t* peekInt32Ref();
    uint32_t readInt32();
    char16_t* readString16(uint32_t len);
};

inline FakeParcel::FakeParcel(char* data) : data(data), cur(0) {}

inline void FakeParcel::skip(uint32_t n) { cur += n; }
inline uint32_t FakeParcel::getCursor() { return cur; }
inline void FakeParcel::skipFlatObj() { skip(FLAT_BINDER_OBJ_SIZE); }

inline uint32_t* FakeParcel::peekInt32Ref() {
    uint32_t* i = ((uint32_t*)(data + cur));
    return i;
}

inline uint32_t FakeParcel::readInt32() {
    uint32_t i = *((uint32_t*)(data + cur));
    skip(sizeof(i));
    return i;
}

inline char16_t* FakeParcel::readString16(uint32_t len) {
    char16_t* s = (char16_t*)(data + cur);
    skip((len + 1) * sizeof(char16_t));  // len+1 (null u16)
    return s;
}

static inline size_t getBinderHeadersLen(int sdk) {
    if (sdk >= 30) return 3 * sizeof(uint32_t);
    else if (sdk == 29) return 2 * sizeof(uint32_t);
    else return 1 * sizeof(uint32_t);
}
