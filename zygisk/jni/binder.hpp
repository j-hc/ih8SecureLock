#pragma once

#include <jni.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(__LP64__)
#define FLAT_BINDER_OBJ_SIZE 24
#else
#define FLAT_BINDER_OBJ_SIZE 16
#endif

#define ARR_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define STR_LEN(a) (ARR_LEN(a) - 1)

#define BINDER_STUB(n) (n "$Stub")
#define BINDER_TRSCTN(n) ("TRANSACTION_" n)

#define BINDER_DESC_CMP(descLit, desc, descLen) \
    (STR_LEN(descLit) == (descLen) &&           \
     memcmp((desc), (descLit), (descLen) * sizeof(char16_t)) == 0)

struct PParcel {
    size_t error;
    char* data;
    size_t data_size;
};

struct FakeParcel {
   private:
    char* data;
    size_t data_size;
    uint32_t cur;

   public:
    FakeParcel(char* data, size_t data_size);

    void skip(uint32_t n);
    uint32_t getCursor();
    void skipFlatObj();

    uint32_t* peekInt32Ref();
    uint32_t readInt32();
    char16_t* readString16(uint32_t len);
};

inline FakeParcel::FakeParcel(char* data, size_t data_size) : data(data), data_size(data_size), cur(0) {}

inline void FakeParcel::skip(uint32_t n) { cur += n; }
inline uint32_t FakeParcel::getCursor() { return cur; }
inline void FakeParcel::skipFlatObj() { skip(FLAT_BINDER_OBJ_SIZE); }

inline uint32_t* FakeParcel::peekInt32Ref() {
    if (cur + sizeof(uint32_t) > data_size) return nullptr;
    uint32_t* i = ((uint32_t*)(data + cur));
    return i;
}

inline uint32_t FakeParcel::readInt32() {
    if (cur + sizeof(uint32_t) > data_size) return 0;
    uint32_t i = *((uint32_t*)(data + cur));
    skip(sizeof(i));
    return i;
}

inline char16_t* FakeParcel::readString16(uint32_t len) {
    if (len == 0 || cur + len > data_size) return nullptr;
    char16_t* s = (char16_t*)(data + cur);
    skip((len + 1) * sizeof(char16_t));  // len+1 (null u16)
    return s;
}

inline size_t getBinderHeadersLen(int sdk) {
    if (sdk >= 30) return 3 * sizeof(uint32_t);
    else if (sdk == 29) return 2 * sizeof(uint32_t);
    else return 1 * sizeof(uint32_t);
}

bool getMapping(const char* lib, ino_t* inode, dev_t* dev);

uint32_t getStaticIntFieldJni(JNIEnv* env, const char* cls_name, const char* field_name);

void companionSendFile(const char* path, int remote_fd);

bool readFullFromFd(int fd, void* buf, off_t size);
