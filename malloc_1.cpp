//
// Created by student on 8/14/24.
//
#include "unistd.h"
void* smalloc(size_t size) {

    if (size == 0) {
        return nullptr;
    }
    if (size > 100000000) {
        return nullptr;
    }

    void * ret = sbrk(size);
    if (ret == (void*)-1){
        return nullptr;
    }
    return ret;
}