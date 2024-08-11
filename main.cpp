#include <iostream>
#include <unistd.h>
#include <cmath>
#include <cstring>
#include <stdio.h>



void* smalloc1(size_t size) {
    if ((size == 0) or size > pow(10, 8))  {
        return nullptr;
    }
    void* start = sbrk(0);
    void* new_break = sbrk(size);  // Increase program break by 100 bytes
    if (new_break == (void*) -1) {
        std::cerr << "sbrk failed!" << std::endl;
        return nullptr;
    }
    return start;

}

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

MallocMetadata * meta_data_global_head = nullptr;
MallocMetadata * meta_data_global_tail = nullptr;



MallocMetadata* find_first_block(size_t size) {
    if (!meta_data_global_head) {
        return nullptr;
    }
    MallocMetadata * head = meta_data_global_head;

    while (head && (head->is_free == false || head->size < size)) {
        head = head->next;
    }
    return head;
}



void* smalloc2(size_t size) {
    if ((size == 0) or size > pow(10, 8))  {
        return nullptr;
    }
    MallocMetadata* block = find_first_block(size);

    if (!block) { // we need to create new block
        void* meta_data_start = sbrk(0);
        void* meta_data_new_break = sbrk(sizeof(MallocMetadata));
        MallocMetadata* new_block = (struct MallocMetadata*) meta_data_start;
        // todo: should we check for an error here?
        new_block->size = size;
        new_block->is_free = false;

        if (!meta_data_global_tail) {

            meta_data_global_tail = new_block;
        }
        else {
            new_block->prev = meta_data_global_tail; // set prev
            meta_data_global_tail->next = new_block; // connect to the tail
            meta_data_global_tail = new_block; // update the new tail
        }

        if (!meta_data_global_head) {
            meta_data_global_head = new_block;
        }
        void* start = sbrk(0);
        void* new_break = sbrk(size);  // Increase program break by 100 bytes
        if (new_break == (void*) -1) {
            std::cerr << "sbrk failed!" << std::endl;
            return nullptr;
            // todo: should we release anything?
        }
        return start;
    }
    block->is_free = false;
    return (void*)(block) + sizeof(MallocMetadata);
}

void* scalloc(size_t num, size_t size) {
    if ((size == 0) or size*num > pow(10, 8))  {
        return nullptr;
    }
    void * ptr = smalloc2(size*num);
    if(ptr)
    {
        memset(ptr, 0, num * size); // todo: can we use this?
    }
    return ptr;
}

void sfree(void* p) {
    if (!p) {
        return;
    }
    MallocMetadata* block =(MallocMetadata*)(p - sizeof (MallocMetadata));
    block->is_free = true;
}

void* srealloc(void* oldp, size_t size) {
    if ((size == 0) or size > pow(10, 8))  {
        return nullptr;
    }
    if (!oldp) {
        return smalloc2(size);
    }
    MallocMetadata* block =(MallocMetadata*)(oldp - sizeof (MallocMetadata));
    if (size <= block->size ) {
        return oldp;
    }
    void* new_p;
    MallocMetadata* new_block = find_first_block(size);
    if (!new_block) {
        new_p = smalloc2(size); // we create new block
        if (!new_p) {
            return nullptr;
        }
    }
    else {
        new_p = (void*)(new_block + sizeof (MallocMetadata)); // we found a block
    }
    memmove(new_p, oldp, size);
    sfree(oldp);
    return new_p;

}

size_t _num_free_blocks() {
    size_t count = 0;
    MallocMetadata* head = meta_data_global_head;
    while (head) {
        if (head->is_free) {
            count += 1;
        }
        head = head->next;
    }
    return count;
}

size_t _num_free_bytes() {
    size_t count = 0;
    MallocMetadata* head = meta_data_global_head;
    while (head) {
        if (head->is_free) {
            count += head->size;
        }
        head = head->next;
    }
    return count;
}

size_t _num_allocated_blocks() {
    size_t count = 0;
    MallocMetadata* head = meta_data_global_head;
    while (head) {
        count += 1;
        head = head->next;
    }
    return count;
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}

size_t _num_meta_data_byte() {
    return _num_allocated_blocks() * _size_meta_data();
}


int main() {
    std::cout << "Hello, World!" << std::endl;
    int *arr = (int*)smalloc2(2* sizeof(int));
    arr[0] = 1;
    arr[1] = 2;
    sfree(arr);
    smalloc2(10);
    smalloc2(10);
    smalloc2(10);
    smalloc2(10);

    return 0;
}
