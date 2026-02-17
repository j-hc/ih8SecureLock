#include "parcel.hpp"

#include <string.h>

#if defined(__LP64__)
#define FLAT_BINDER_OBJ_SIZE 24
#else
#define FLAT_BINDER_OBJ_SIZE 16
#endif

FakeParcel::FakeParcel(char* data) : data(data), cur(0) {}

void FakeParcel::skip(uint32_t n) {
    cur += n;
}
void FakeParcel::skipFlatObj() {
    skip(FLAT_BINDER_OBJ_SIZE);
}

uint32_t* FakeParcel::peekInt32Ref() {
    uint32_t* i = ((uint32_t*)(data + cur));
    return i;
}

uint32_t FakeParcel::readInt32() {
    uint32_t i = *((uint32_t*)(data + cur));
    skip(sizeof(i));
    return i;
}

char16_t* FakeParcel::readString16(uint32_t len) {
    char16_t* s = (char16_t*)(data + cur);
    skip((len + 1) * sizeof(char16_t));  // len+1 (null u16)
    return s;
}

bool FakeParcel::checkInterface(const char16_t* desc, uint32_t desc_len) {
    uint32_t len = readInt32();  // desc len
    if (len != desc_len) return false;

    auto desc_read = readString16(len);  // desc
    return memcmp(desc_read, desc, desc_len * sizeof(char16_t)) == 0;
}
