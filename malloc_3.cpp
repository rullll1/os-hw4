#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/mman.h>

constexpr int MAX_ORDER = 10;
constexpr size_t INITIAL_BLOCK_SIZE = 32 * 131072;
constexpr size_t MAX_ALLOCATION_SIZE = 100000000;

struct MallocMetadata {
    bool is_free = true;
    size_t size = 0;
    int order = 0;
    MallocMetadata *next = nullptr;
    MallocMetadata *prev = nullptr;
    MallocMetadata *next_ordered = nullptr;
    MallocMetadata *prev_ordered = nullptr;
};

constexpr size_t METADATA_SIZE = sizeof(MallocMetadata);

struct MemoryStats {
    size_t num_free_bytes = 0;
    size_t num_free_blocks = 0;
    size_t num_allocated_blocks = 0;
    size_t num_allocated_bytes = 0;
};

MallocMetadata *block_list[11] = {nullptr};
MemoryStats memory_stats;

bool blocks_init = false;

constexpr size_t size_of_block(int order) {
    return 128 * (1 << order);
}

void list_insert(MallocMetadata *metadata) {
    auto &head = block_list[metadata->order];
    if (head == nullptr) {
        metadata->next_ordered = nullptr;
        metadata->prev_ordered = nullptr;
        head = metadata;
        return;
    }

    for (MallocMetadata *iter_curr = head; iter_curr != nullptr; iter_curr = iter_curr->next_ordered) {
        if (iter_curr > metadata) {
            metadata->next_ordered = iter_curr;
            metadata->prev_ordered = iter_curr->prev_ordered;
            iter_curr->prev_ordered = metadata;
            if (metadata->prev_ordered == nullptr) {
                head = metadata;
            } else {
                metadata->prev_ordered->next_ordered = metadata;
            }
            return;
        }
        if (iter_curr->next_ordered == nullptr) {
            iter_curr->next_ordered = metadata;
            metadata->prev_ordered = iter_curr;
            metadata->next_ordered = nullptr;
            return;
        }
    }
}

void list_remove(MallocMetadata *metadata) {
    auto &head = block_list[metadata->order];
    for (MallocMetadata *iter = head; iter != nullptr; iter = iter->next_ordered) {
        if (iter == metadata) {
            if (iter->prev_ordered == nullptr) {
                head = iter->next_ordered;
            } else {
                iter->prev_ordered->next_ordered = iter->next_ordered;
            }
            if (iter->next_ordered != nullptr) {
                iter->next_ordered->prev_ordered = iter->prev_ordered;
            }
            return;
        }
    }
}

MallocMetadata *split_blocks(MallocMetadata *metadata_to_split) {
    if (!metadata_to_split || metadata_to_split->order == 0) return metadata_to_split;

    list_remove(metadata_to_split);

    size_t half_size = size_of_block(metadata_to_split->order - 1);
    auto *new_meta = reinterpret_cast<MallocMetadata *>(reinterpret_cast<char *>(metadata_to_split) + half_size);

    new_meta->order = metadata_to_split->order - 1;
    new_meta->is_free = true;
    new_meta->next = metadata_to_split->next;
    new_meta->prev = metadata_to_split;

    metadata_to_split->order--;
    metadata_to_split->next = new_meta;

    if (new_meta->next) {
        new_meta->next->prev = new_meta;
    }

    list_insert(new_meta);
    list_insert(metadata_to_split);

    memory_stats.num_free_blocks++;
    memory_stats.num_free_bytes -= METADATA_SIZE;
    memory_stats.num_allocated_blocks++;
    memory_stats.num_allocated_bytes -= METADATA_SIZE;

    return metadata_to_split;
}

MallocMetadata *split_memory(size_t size, MallocMetadata *metadata = nullptr) {
    MallocMetadata *iter = nullptr;
    int order = -1;

    if (metadata == nullptr) {
        for (int i = 0; i <= MAX_ORDER; i++) {
            iter = block_list[i];
            if (iter && size <= size_of_block(i) - METADATA_SIZE) {
                order = i;
                break;
            }
        }
        if (order == -1) return nullptr;
    } else {
        iter = metadata;
        order = metadata->order;
    }

    while (order > 0 && size <= size_of_block(order - 1) - METADATA_SIZE) {
        iter = split_blocks(iter);
        order--;
    }

    return iter;
}

void init_blocks() {
    if (blocks_init) return;

    void *block_ptr = sbrk(0);
    size_t align = INITIAL_BLOCK_SIZE - (reinterpret_cast<uintptr_t>(block_ptr) % INITIAL_BLOCK_SIZE);
    if (sbrk(INITIAL_BLOCK_SIZE + align) == reinterpret_cast<void *>(-1)) return;

    block_ptr = reinterpret_cast<void *>(reinterpret_cast<char *>(block_ptr) + align);
    block_list[MAX_ORDER] = static_cast<MallocMetadata *>(block_ptr);

    MallocMetadata *iter = block_list[MAX_ORDER];
    iter->is_free = true;
    iter->order = MAX_ORDER;
    iter->prev_ordered = nullptr;
    iter->prev = nullptr;
    constexpr size_t num_blocks = INITIAL_BLOCK_SIZE / size_of_block(MAX_ORDER);

    for (size_t i = 0; i < num_blocks - 1; i++) {
        iter->next = reinterpret_cast<MallocMetadata *>(reinterpret_cast<char *>(iter) + size_of_block(MAX_ORDER));
        iter->next->prev = iter;
        iter->next_ordered = iter->next;
        iter->next_ordered->prev_ordered = iter;
        iter = iter->next;
        iter->order = MAX_ORDER;
        iter->is_free = true;
    }
    iter->next_ordered = nullptr;
    iter->next = nullptr;
    memory_stats.num_allocated_blocks += num_blocks;
    memory_stats.num_free_blocks += num_blocks;
    memory_stats.num_allocated_bytes += num_blocks * (size_of_block(MAX_ORDER) - METADATA_SIZE);
    memory_stats.num_free_bytes += num_blocks * (size_of_block(MAX_ORDER) - METADATA_SIZE);
    blocks_init = true;
}

void* allocate_large_block(size_t size) {
    void *ptr = mmap(nullptr, size + METADATA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == reinterpret_cast<void *>(-1)) return nullptr;

    auto *meta = static_cast<MallocMetadata *>(ptr);
    meta->size = size;
    meta->is_free = false;

    memory_stats.num_allocated_bytes += size;
    memory_stats.num_allocated_blocks++;

    return reinterpret_cast<char *>(meta) + METADATA_SIZE;
}

void* allocate_small_block(size_t size) {
    MallocMetadata *block = split_memory(size);
    if (!block) return nullptr;

    block->is_free = false;
    list_remove(block);

    memory_stats.num_free_blocks--;
    memory_stats.num_free_bytes -= size_of_block(block->order) - METADATA_SIZE;

    return reinterpret_cast<char *>(block) + METADATA_SIZE;
}

MallocMetadata *merge_blocks(MallocMetadata *first_metadata, MallocMetadata *second_metadata) {
    if (!first_metadata || !second_metadata) return nullptr;

    list_remove(first_metadata);
    list_remove(second_metadata);

    if (first_metadata > second_metadata) std::swap(first_metadata, second_metadata);

    first_metadata->next = second_metadata->next;
    if (second_metadata->next) {
        second_metadata->next->prev = first_metadata;
    }

    first_metadata->order++;
    list_insert(first_metadata);

    memory_stats.num_free_bytes += METADATA_SIZE;
    memory_stats.num_free_blocks--;
    memory_stats.num_allocated_bytes += METADATA_SIZE;
    memory_stats.num_allocated_blocks--;

    return first_metadata;
}

MallocMetadata *merge_memory(MallocMetadata *metadata) {
    MallocMetadata *iter = metadata;
    for (int i = metadata->order; i < MAX_ORDER; i++) {
        auto *buddy = reinterpret_cast<MallocMetadata *>(reinterpret_cast<uintptr_t>(iter) ^ size_of_block(i));
        if (iter->order != buddy->order || !buddy->is_free) return iter;

        iter = merge_blocks(iter, buddy);
    }
    return iter;
}

void *smalloc(size_t size) {
    if (!blocks_init) init_blocks();
    if (size == 0 || size > MAX_ALLOCATION_SIZE) return nullptr;
    if (size >= size_of_block(MAX_ORDER)) return allocate_large_block(size);

    return allocate_small_block(size);
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
    if (!p) return;

    auto *meta = reinterpret_cast<MallocMetadata *>(reinterpret_cast<char *>(p) - METADATA_SIZE);
    if (meta->is_free) return;

    if (meta->size > 0) {
        memory_stats.num_allocated_blocks--;
        memory_stats.num_allocated_bytes -= meta->size;

        munmap(meta, meta->size + METADATA_SIZE);
    } else {
        meta->is_free = true;
        memory_stats.num_free_blocks++;
        memory_stats.num_free_bytes += size_of_block(meta->order) - METADATA_SIZE;

        list_insert(meta);
        merge_memory(meta);
    }
}

void* allocate_new_block(size_t size, void* oldp, size_t oldSize) {
    void* newPtr = smalloc(size);
    if (newPtr == nullptr) {
        return nullptr; // Allocation failed
    }
    memmove(newPtr, oldp, oldSize);
    sfree(oldp);
    return newPtr;
}

void* handle_large_allocation(MallocMetadata* block, void* oldp, size_t size) {
    if (block->size == size) {
        return oldp;
    }
    return allocate_new_block(size, oldp, block->size);
}

MallocMetadata* merge_free_blocks(MallocMetadata* block, size_t size) {
    MallocMetadata* iter = block;
    for (int i = iter->order; i < MAX_ORDER; i++) {
        auto* buddy = reinterpret_cast<MallocMetadata*>(reinterpret_cast<uintptr_t>(iter) ^ size_of_block(i));

        if (!buddy->is_free) {
            break;
        }

        if (size <= size_of_block(i + 1) - METADATA_SIZE) {
            list_insert(block);
            block->is_free = true;

            for (int j = iter->order; j <= i; j++) {
                buddy = reinterpret_cast<MallocMetadata*>(reinterpret_cast<uintptr_t>(iter) ^ size_of_block(j));
                iter = merge_blocks(iter, buddy);

                if (iter == nullptr) {
                    return nullptr;
                }

                memory_stats.num_free_bytes -= size_of_block(iter->order - 1);
            }

            iter->is_free = false;
            list_remove(iter);
            return iter;
        }
    }
    return nullptr;
}


void* srealloc(void* oldp, size_t size) {
    if (size == 0 || size > MAX_ALLOCATION_SIZE) return nullptr;
    if (!oldp) return smalloc(size);

    auto *block = reinterpret_cast<MallocMetadata *>(reinterpret_cast<char *>(oldp) - METADATA_SIZE);
    if (size >= 131072) {
        return handle_large_allocation(block, oldp, size);
    }

    if (size <= size_of_block(block->order)) return oldp;

    auto *new_block = merge_free_blocks(block, size);
    if (new_block) {
        memmove(reinterpret_cast<char *>(new_block) + METADATA_SIZE, oldp, size_of_block(block->order) - METADATA_SIZE);
        return reinterpret_cast<char *>(new_block) + METADATA_SIZE;
    }

    return allocate_new_block(size, oldp, size_of_block(block->order) - METADATA_SIZE);
}


size_t _num_free_blocks() { return memory_stats.num_free_blocks; }

size_t _num_free_bytes() { return memory_stats.num_free_bytes; }

size_t _num_allocated_blocks() { return memory_stats.num_allocated_blocks; }

size_t _num_allocated_bytes() { return memory_stats.num_allocated_bytes; }

size_t _num_meta_data_bytes() { return METADATA_SIZE * _num_allocated_blocks(); }

size_t _size_meta_data() { return METADATA_SIZE; }