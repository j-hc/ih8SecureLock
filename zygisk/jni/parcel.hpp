#pragma once

#include <stdint.h>

struct PParcel {
    size_t error;
    char* data;
    size_t data_size;
};

struct FakeParcel {
    char* data;
    uint32_t cur;

    FakeParcel(char* data);

    void skip(uint32_t n);
    void skipFlatObj();

    uint32_t* peekInt32Ref();
    uint32_t readInt32();
    char16_t* readString16(uint32_t len);

    bool checkInterface(const char16_t* desc, uint32_t desc_len);
};
