#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <cmath>
#include <sys/mman.h>

#define MAX_ORDER 10

struct MallocMetadata {
    bool is_free = true;
    size_t mmap_alloc_size = 0;
    int order = 0;
    MallocMetadata *next = nullptr;
    MallocMetadata *prev = nullptr;
    MallocMetadata *next_in_order = nullptr;
    MallocMetadata *prev_in_order = nullptr;
};

bool orderBlocksInit = false;
size_t num_of_free_bytes = 0;
size_t num_of_free_blocks = 0;
size_t num_of_alloc_blocks = 0;
size_t num_of_alloc_bytes = 0;

MallocMetadata *orderBlocks[11] = {nullptr};
MallocMetadata *_memoryBlocks = nullptr;

void orderListInsert(MallocMetadata *metadata) {
    auto &head = orderBlocks[metadata->order];
    if (head == nullptr) {
        metadata->next_in_order = nullptr;
        metadata->prev_in_order = nullptr;
        head = metadata;
        return;
    }

    for (MallocMetadata *iter_curr = head; iter_curr != nullptr; iter_curr = iter_curr->next_in_order) {
        if (iter_curr > metadata) {
            metadata->next_in_order = iter_curr;
            metadata->prev_in_order = iter_curr->prev_in_order;
            iter_curr->prev_in_order = metadata;
            if (metadata->prev_in_order == nullptr) {
                head = metadata;
            } else {
                metadata->prev_in_order->next_in_order = metadata;
            }
            return;
        }
        if (iter_curr->next_in_order == nullptr) {
            iter_curr->next_in_order = metadata;
            metadata->prev_in_order = iter_curr;
            metadata->next_in_order = nullptr;
            return;
        }
    }
}

void orderListRemove(MallocMetadata *metadata) {
    auto &head = orderBlocks[metadata->order];
    for (MallocMetadata *iter = head; iter != nullptr; iter = iter->next_in_order) {
        if (iter == metadata) {
            if (iter->prev_in_order == nullptr) {
                head = iter->next_in_order;
            } else {
                iter->prev_in_order->next_in_order = iter->next_in_order;
            }
            if (iter->next_in_order != nullptr) {
                iter->next_in_order->prev_in_order = iter->prev_in_order;
            }
            return;
        }
    }
}

MallocMetadata *mergeBlocks(MallocMetadata *first_metadata, MallocMetadata *second_metadata) {
    if(first_metadata == nullptr || second_metadata == nullptr)
    {
        return nullptr;
    }
    orderListRemove(first_metadata);
    orderListRemove(second_metadata);
    num_of_free_blocks--;
    num_of_free_bytes += sizeof(MallocMetadata);
    num_of_alloc_blocks--;
    num_of_alloc_bytes += sizeof(MallocMetadata);
    if (first_metadata > second_metadata) {
        MallocMetadata *tmp = first_metadata;
        first_metadata = second_metadata;
        second_metadata = tmp;
    }
    first_metadata->next = second_metadata->next;
    if (second_metadata->next != nullptr) {
        second_metadata->next->prev = first_metadata;
    }
    first_metadata->order = first_metadata->order + 1;
    orderListInsert(first_metadata);
    return first_metadata;
}

MallocMetadata *splitBlocks(MallocMetadata *metadata_to_split) {
    if(metadata_to_split == nullptr)
    {
        return nullptr;
    }
    if (metadata_to_split->order == 0)
    {
        return metadata_to_split;
    }
    num_of_free_blocks++;
    num_of_free_bytes -= sizeof(MallocMetadata);
    num_of_alloc_blocks++;
    num_of_alloc_bytes -= sizeof(MallocMetadata);
    orderListRemove(metadata_to_split);
    auto *new_meta = static_cast<MallocMetadata *>((void *) ((char *) metadata_to_split + (int) (128 * pow(2, (metadata_to_split->order - 1)))));
    metadata_to_split->order = metadata_to_split->order - 1;
    new_meta->order = metadata_to_split->order;
    new_meta->is_free = true;
    metadata_to_split->is_free = true;
    new_meta->next = metadata_to_split->next;
    new_meta->prev = metadata_to_split;
    metadata_to_split->next = new_meta;
    if (new_meta->next != nullptr)
    {
        new_meta->next->prev = new_meta;
    }
    orderListInsert(new_meta);
    orderListInsert(metadata_to_split);
    return metadata_to_split;
}

MallocMetadata *memorySplit(size_t size, MallocMetadata *metadata = nullptr) {
    MallocMetadata *iter = nullptr;
    int order = -1;
    if (metadata == nullptr) {
        for (int i = 0; i <= MAX_ORDER; i++) {
            iter = orderBlocks[i];
            if (iter == nullptr || size > (size_t)((128 * pow(2,i)) - sizeof(MallocMetadata))) {
                continue;
            }
            else{
                order = iter->order;
                if(order == 0 || size > (size_t)((128 * pow(2,(order - 1))) - sizeof(MallocMetadata)))
                {
                    return iter;
                }
                break;
            }
        }
        if(order == -1)
        {
            return nullptr;
        }
    }
    else
    {
        order = metadata->order;
        if(order == 0 || size > (size_t)((128 * pow(2,(order - 1))) - sizeof(MallocMetadata)))
        {
            return metadata;
        }
    }
    for (int j = order; j > 0; j--) {
        iter = splitBlocks(iter);
        if (iter == nullptr) {
            return nullptr;
        }
        if ((size > (size_t)(128 * pow(2,(iter->order - 1)) - sizeof(MallocMetadata)) || iter->order == 0))
        {
            return iter;
        }
    }
    return metadata;
}

MallocMetadata *memoryMerge(MallocMetadata *metadata) {
    MallocMetadata *iter = metadata;
    MallocMetadata *buddy;
    for (int i = metadata->order; i < MAX_ORDER; i++) {
        buddy = (MallocMetadata *) ((void *) ((std::uintptr_t) iter ^ (int)(128 * pow(2,(iter->order)))));
        if (metadata->order != buddy->order || !buddy->is_free)
        {
            return metadata;
        }
        iter = mergeBlocks(iter, buddy);
    }
    return iter;
}

size_t _sizeOfBlock(MallocMetadata *block) {
    return 128 * (int)pow(2,block->order);
}

void *smalloc(size_t size) {
    if (!orderBlocksInit) {
        void *block_ptr = sbrk(0);
        size_t align = (32 * 131072) - (((uintptr_t) block_ptr) % (32 * 131072));
        if (sbrk((32 * 131072) + align) == (void *) -1) {
            return nullptr;
        }
        block_ptr = (void *) ((char *) block_ptr + align);
        orderBlocks[MAX_ORDER] = (MallocMetadata *) block_ptr;
        MallocMetadata *iter = orderBlocks[MAX_ORDER];
        iter->is_free = true;
        iter->order = MAX_ORDER;
        iter->prev_in_order = nullptr;
        iter->prev = nullptr;
        for (int i = 0; i < 31; i++) {
            iter->next = (MallocMetadata *) ((char *) iter + 131072);
            iter->next->prev = iter;
            iter->next_in_order = iter->next;
            iter->next_in_order->prev_in_order = iter;
            iter = iter->next;
            iter->order = MAX_ORDER;
            iter->is_free = true;
        }
        iter->next_in_order = nullptr;
        iter->next = nullptr;
        num_of_alloc_blocks += 32;
        num_of_free_blocks += 32;
        num_of_alloc_bytes += 32 * (131072 - sizeof(MallocMetadata));
        num_of_free_bytes += 32 * (131072 - sizeof(MallocMetadata));
        orderBlocksInit = true;
    }
    if (size == 0 || size > 100000000) {
        return nullptr;
    }
    if (size >= 131072) {
        void *ptr = mmap(NULL, size + sizeof(MallocMetadata), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == (void *) -1) {
            return nullptr;
        }
        MallocMetadata *meta = (MallocMetadata *) ptr;
        meta->mmap_alloc_size = size;
        meta->is_free = false;
        num_of_alloc_bytes += size;
        num_of_alloc_blocks++;
        return (void *) ((char *) meta + sizeof(MallocMetadata));
    }
    else {
        MallocMetadata *block = memorySplit(size);
        if (block == nullptr) {
            return nullptr;
        }
        block->is_free = false;
        orderListRemove(block);
        num_of_free_blocks--;
        num_of_free_bytes -= ((int)pow(2,block->order) * 128 - sizeof(MallocMetadata));
        return (void *) ((char *) block + sizeof(MallocMetadata));
    }
}

void *scalloc(size_t num, size_t size) {
    int real_size = num * size;
    void *block_ptr = smalloc(real_size);
    if (block_ptr != nullptr) {
        memset(block_ptr, 0, real_size);
    }
    return block_ptr;
}

void sfree(void *p) {
    if (p == nullptr)
    {
        return;
    }
    MallocMetadata *meta = (MallocMetadata *) ((char *) p - sizeof(MallocMetadata));
    if(meta->is_free)
    {
        return;
    }
    if (meta->mmap_alloc_size > 0) {
        num_of_alloc_bytes -= meta->mmap_alloc_size;
        num_of_alloc_blocks--;
        munmap(meta, _sizeOfBlock(meta) + sizeof(MallocMetadata));
    }
    else {
        meta->is_free = true;
        num_of_free_blocks++;
        num_of_free_bytes += (_sizeOfBlock(meta) - sizeof(MallocMetadata));
        orderListInsert(meta);
        meta = memoryMerge(meta);
    }
}

void *srealloc(void *oldp, size_t size) {
    if (size == 0 || size > 100000000) {
        return nullptr;
    }
    if (oldp == nullptr) {
        return smalloc(size);
    }
    MallocMetadata *block = (MallocMetadata *) ((char *) oldp - sizeof(MallocMetadata));
    if (size >= 131072) {
        if(block->mmap_alloc_size == size){
            return oldp;
        }
        void *ptr = smalloc(size);
        memmove(ptr, oldp, block->mmap_alloc_size);
        sfree(oldp);
        return (void *) ((char *) ptr + sizeof(MallocMetadata));
    }
    if (size <= _sizeOfBlock(block)) {
        return oldp;
    }
    else {
        MallocMetadata *iter = block;
        MallocMetadata *buddy;
        for (int i = iter->order; i < MAX_ORDER; i++) {
            buddy = (MallocMetadata *) ((void *) ((std::uintptr_t) iter ^ (int) (128 * pow(2, i))));
            if (!buddy->is_free) {
                break;
            }
            if (size <= 128 * (int)pow(2,(i + 1)) - sizeof(MallocMetadata)) {
                orderListInsert(block);
                block->is_free = true;
                for (int j = iter->order; j <= i; j++) {
                    buddy = (MallocMetadata *) ((void *) ((std::uintptr_t) iter ^ (int)(128 * pow(2,j))));
                    iter = mergeBlocks(iter, buddy);
                    num_of_free_bytes -= ((int)pow(2,iter->order-1) * 128);
                }
                iter->is_free=false;
                orderListRemove(iter);
                memmove((char *) iter + sizeof(MallocMetadata), oldp, _sizeOfBlock(block) -sizeof(MallocMetadata));
                return (char *) iter + sizeof(MallocMetadata);
            }
        }
    }
    sfree(oldp);
    return smalloc(size);
}

size_t _num_free_blocks() {
    return num_of_free_blocks;
}

size_t _num_free_bytes() {
    return num_of_free_bytes;
}

size_t _num_allocated_blocks() {
    return num_of_alloc_blocks;
}

size_t _num_allocated_bytes() {
    return num_of_alloc_bytes;
}

size_t _num_meta_data_bytes() {
    return sizeof(MallocMetadata) * _num_allocated_blocks();
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}