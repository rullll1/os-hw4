#include <unistd.h>
#include <cstring>

constexpr size_t MAX_ALLOCATION_SIZE = 100000000;

struct MallocMetadata {
    size_t size;
    bool is_free = false;
    MallocMetadata* next = nullptr;
    MallocMetadata* prev = nullptr;
};

constexpr size_t METADATA_SIZE = sizeof(MallocMetadata);

MallocMetadata* memoryBlocks = nullptr;

MallocMetadata* find_free(const size_t size)
{
    if(memoryBlocks == nullptr || size <= 0)
    {
        return nullptr;
    }
    MallocMetadata* iter = memoryBlocks;
    while(iter->next != nullptr)
    {
        if(iter->is_free && size <= iter->size)
        {
            return iter;
        }
        iter = iter->next;
    }
    return iter;
}

void* smalloc(size_t size) {
    if (size == 0 || size > MAX_ALLOCATION_SIZE) {
        return nullptr;
    }

    MallocMetadata* block = find_free(size);

    if (block == nullptr || (block->next == nullptr && !block->is_free)) {
        void* block_ptr = sbrk(0);
        if (sbrk(METADATA_SIZE + size) == reinterpret_cast<void *>(-1)) {
            return nullptr;
        }

        auto* new_block = static_cast<MallocMetadata *>(block_ptr);
        new_block->size = size;

        if (block == nullptr) {
            memoryBlocks = new_block;
        } else {
            block->next = new_block;
            new_block->prev = block;
        }

        return reinterpret_cast<char *>(new_block) + METADATA_SIZE;
    }

    block->is_free = false;
    return reinterpret_cast<char *>(block) + METADATA_SIZE;
}

void* scalloc(size_t num, size_t size) {
    size_t real_size = num * size;
    void* block_ptr = smalloc(real_size);
    if (block_ptr != nullptr) {
        std::memset(block_ptr, 0, real_size);
    }
    return block_ptr;
}

void sfree(void* p) {
    if (p == nullptr) {
        return;
    }

    auto* block = reinterpret_cast<MallocMetadata *>(static_cast<char *>(p) - METADATA_SIZE);
    block->is_free = true;
}

void* srealloc(void* oldp, size_t size) {
    if (size == 0 || size > MAX_ALLOCATION_SIZE) {
        return nullptr;
    }

    if (oldp == nullptr) {
        return smalloc(size);
    }

    auto* block = reinterpret_cast<MallocMetadata *>(static_cast<char *>(oldp) - METADATA_SIZE);

    if (size <= block->size) {
        return oldp;
    }

    void* new_block_ptr = smalloc(size);
    if (new_block_ptr != nullptr) {
        std::memmove(new_block_ptr, oldp, block->size);
        sfree(oldp);
    }

    return new_block_ptr;
}

size_t _num_free_blocks() {
    size_t count = 0;
    for (const MallocMetadata* iter = memoryBlocks; iter != nullptr; iter = iter->next) {
        if (iter->is_free) {
            count++;
        }
    }
    return count;
}

size_t _num_free_bytes() {
    size_t total_size = 0;
    for (const MallocMetadata* iter = memoryBlocks; iter != nullptr; iter = iter->next) {
        if (iter->is_free) {
            total_size += iter->size;
        }
    }
    return total_size;
}

size_t _num_allocated_blocks() {
    size_t count = 0;
    for (const MallocMetadata* iter = memoryBlocks; iter != nullptr; iter = iter->next) {
        count++;
    }
    return count;
}

size_t _num_allocated_bytes() {
    size_t total_size = 0;
    for (const MallocMetadata* iter = memoryBlocks; iter != nullptr; iter = iter->next) {
        total_size += iter->size;
    }
    return total_size;
}

size_t _num_meta_data_bytes() {
    return METADATA_SIZE * _num_allocated_blocks();
}

size_t _size_meta_data() {
    return METADATA_SIZE;
}