//
// Created by hzj on 25-1-10.
//
/**
 * AppShift Memory Pool v2.0.0
 *
 * Copyright 2020-present Sapir Shemer, DevShift (devshift.biz)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @author Sapir Shemer
 */

#include "include/MemoryPool.h"
#include <iostream>
#include <mutex>
#include <sys/mman.h>

namespace co {

    MemoryPool::MemoryPool(size_t block_size, bool single_block, bool use_mmap, int mmap_flag) {
        // Add first block to memory pool
        this->firstBlock = this->currentBlock = nullptr;
        this->defaultBlockSize = block_size;
        this->currentScope = nullptr;
        this->createMemoryBlock(block_size);
        this->single_block = single_block;
        this->use_mmap = use_mmap;
        this->mmap_flag = mmap_flag;
    }

    MemoryPool::~MemoryPool() {
        SMemoryBlockHeader *block_iterator = firstBlock;

        while (block_iterator != nullptr) {
            SMemoryBlockHeader *next_iterator = block_iterator->next;
            std::free(block_iterator);
            block_iterator = next_iterator;
        }
    }

    void MemoryPool::createMemoryBlock(size_t block_size) {
        // Create the block
        SMemoryBlockHeader *block{};
        if (!use_mmap)
            block = reinterpret_cast<SMemoryBlockHeader *>(std::malloc(sizeof(SMemoryBlockHeader) + block_size));
        else
            block = reinterpret_cast<SMemoryBlockHeader *>(mmap(0, sizeof(SMemoryBlockHeader) + block_size,
                                                                PROT_READ | PROT_WRITE, mmap_flag, -1, 0));

        //memset(block, 0, sizeof(SMemoryBlockHeader) + block_size);
        if (block == nullptr)
            throw EMemoryErrors::CANNOT_CREATE_BLOCK;

        // Initalize block data
        block->blockSize = block_size;
        block->offset = 0;
        block->numberOfAllocated = 0;
        block->numberOfDeleted = 0;

        if (this->firstBlock != nullptr) {
            block->next = nullptr;
            block->prev = this->currentBlock;
            this->currentBlock->next = block;
            this->currentBlock = block;
        } else {
            block->next = block->prev = nullptr;
            this->firstBlock = block;
            this->currentBlock = block;
        }
    }

    void *MemoryPool::allocate_unsafe(size_t instances) {
        size_t real_size = instances + (ALIGN - sizeof(SMemoryUnitHeader));
        if (real_size + sizeof(SMemoryUnitHeader) >= this->currentBlock->blockSize - this->currentBlock->offset
            && this->single_block)
            return nullptr;

        // If there is enough space in current block then use the current block
        if (real_size + sizeof(SMemoryUnitHeader) < this->currentBlock->blockSize - this->currentBlock->offset);
            // Create new block if not enough space
        else if (real_size + sizeof(SMemoryUnitHeader) >= this->defaultBlockSize) {
            this->createMemoryBlock(real_size + sizeof(SMemoryUnitHeader));
        } else {
            this->createMemoryBlock(this->defaultBlockSize);
        }

        // Add unit
        auto *unit = reinterpret_cast<SMemoryUnitHeader *>(reinterpret_cast<char *>(this->currentBlock) +
                                                           sizeof(SMemoryBlockHeader) + this->currentBlock->offset);
        unit->length = real_size;
        unit->container = this->currentBlock;
        this->currentBlock->numberOfAllocated++;
        this->currentBlock->offset += sizeof(SMemoryUnitHeader) + real_size;

        auto ans = reinterpret_cast<char *>(unit) + sizeof(SMemoryUnitHeader);
        auto aligned_ans = ans + (ALIGN - sizeof(SMemoryUnitHeader));
        return (void *) aligned_ans;
    }

    void *MemoryPool::allocate(size_t instances) {
        std::lock_guard<spin_lock> lock(m_lock);
        return allocate_unsafe(instances);
    }

    template<typename T, typename ... Args>
    T *MemoryPool::newElem(Args &&... args) {
        auto ptr = allocate_unsafe(sizeof(T));
        if (ptr == nullptr)
            return nullptr;

        ptr = new(ptr) T(args...);
        return ptr;
    }

    void *MemoryPool::reallocate(void *unit_pointer_start, size_t new_size) {
        if (unit_pointer_start == nullptr) return nullptr;

        // Find unit
        auto *unit = reinterpret_cast<SMemoryUnitHeader *>(reinterpret_cast<char *>(unit_pointer_start) -
                                                           sizeof(SMemoryUnitHeader));
        SMemoryBlockHeader *block = unit->container;

        // If last in block && enough space in block, then reset length
        if (reinterpret_cast<char *>(block) + sizeof(SMemoryBlockHeader) + block->offset ==
            reinterpret_cast<char *>(unit) + sizeof(SMemoryUnitHeader) + unit->length
            && block->blockSize > block->offset + new_size - unit->length) {
            block->offset += new_size - unit->length;
            unit->length = new_size;

            return unit_pointer_start;
        }

        // Allocate new and free previous
        void *temp_point = this->allocate_unsafe(new_size);
        std::memcpy(temp_point, unit_pointer_start, unit->length);
        this->free_unsafe(unit_pointer_start);

        return temp_point;
    }

    void MemoryPool::free_unsafe(void *unit_pointer_start) {
        if (unit_pointer_start == nullptr)
            return;

        // Find unit
        auto *unit = reinterpret_cast<SMemoryUnitHeader *>(reinterpret_cast<char *>(unit_pointer_start) -
                                                           sizeof(SMemoryUnitHeader) -
                                                           (ALIGN - sizeof(SMemoryUnitHeader)));
        SMemoryBlockHeader *block = unit->container;

        // If last in block, then reset offset
        if (reinterpret_cast<char *>(block) + sizeof(SMemoryBlockHeader) + block->offset ==
            reinterpret_cast<char *>(unit) + sizeof(SMemoryUnitHeader) + unit->length) {
            block->offset -= sizeof(SMemoryUnitHeader) + unit->length;
            block->numberOfAllocated--;
        } else
            block->numberOfDeleted++;

        // If block offset is 0 remove block if not the only one left
        if (this->currentBlock != this->firstBlock &&
            (block->offset == 0 || block->numberOfAllocated == block->numberOfDeleted)) {
            if (block == this->firstBlock) {
                this->firstBlock = block->next;
                this->firstBlock->prev = nullptr;
            } else if (block == this->currentBlock) {
                this->currentBlock = block->prev;
                this->currentBlock->next = nullptr;
            } else {
                block->prev->next = block->next;
                block->next->prev = block->prev;
            }

            if (!use_mmap)
                std::free(block);
            else
                munmap(block, sizeof(SMemoryBlockHeader) + block->blockSize);
        }
    }

    void MemoryPool::deallocate(void *unit_pointer_start) {
        if (unit_pointer_start == nullptr)
            return;

        // Find unit
        auto *unit = reinterpret_cast<SMemoryUnitHeader *>(reinterpret_cast<char *>(unit_pointer_start) -
                                                           sizeof(SMemoryUnitHeader) -
                                                           (ALIGN - sizeof(SMemoryUnitHeader)));
        SMemoryBlockHeader *block = unit->container;

        // If last in block, then reset offset
        m_lock.lock();
        if (reinterpret_cast<char *>(block) + sizeof(SMemoryBlockHeader) + block->offset ==
            reinterpret_cast<char *>(unit) + sizeof(SMemoryUnitHeader) + unit->length) {
            block->offset -= sizeof(SMemoryUnitHeader) + unit->length;
            block->numberOfAllocated--;
        } else
            block->numberOfDeleted++;

        // If block offset is 0 remove block if not the only one left
        if (this->currentBlock != this->firstBlock &&
            (block->offset == 0 || block->numberOfAllocated == block->numberOfDeleted)) {
            if (block == this->firstBlock) {
                this->firstBlock = block->next;
                this->firstBlock->prev = nullptr;
            } else if (block == this->currentBlock) {
                this->currentBlock = block->prev;
                this->currentBlock->next = nullptr;
            } else {
                block->prev->next = block->next;
                block->next->prev = block->prev;
            }

            m_lock.unlock();
            if (!use_mmap)
                std::free(block);
            else
                munmap(block, sizeof(SMemoryBlockHeader) + block->blockSize);
        } else {
            m_lock.unlock();
        }
    }

    void MemoryPool::dumpPoolData() {
        SMemoryBlockHeader *block = this->firstBlock;
        SMemoryUnitHeader *unit;

        size_t current_unit_offset;
        size_t block_counter = 1;
        size_t unit_counter = 1;

        while (block != nullptr) {
            // Dump block data
            std::cout << "Block " << block_counter << ": " << std::endl;
            std::cout << "\t" << "Used: " << (float) (block->offset) / (float) (block->blockSize) * 100 << "% " << "("
                      << block->offset << "/" << block->blockSize << ")" << std::endl;

            if (block->offset == 0) {
                block = block->next;
                block_counter++;
                continue;
            }

            std::cout << "\t" << "Units: ========================" << std::endl;
            current_unit_offset = 0;
            unit_counter = 1;
            while (current_unit_offset < block->offset) {
                unit = reinterpret_cast<SMemoryUnitHeader *>(reinterpret_cast<char *>(block + 1) + current_unit_offset);
                std::cout << "\t\t" << "Unit " << unit_counter << ": " << unit->length + sizeof(SMemoryUnitHeader)
                          << std::endl;
                current_unit_offset += sizeof(SMemoryUnitHeader) + unit->length;
                unit_counter++;
            }

            std::cout << "\t" << "===============================" << std::endl;

            block = block->next;
            block_counter++;
        }
    }

    void MemoryPool::startScope() {
        // Create new scope, on top of previous if exists
        if (this->currentScope == nullptr) {
            this->currentScope = new(this) SMemoryScopeHeader;
            this->currentScope->prevScope = nullptr;
        } else {
            SMemoryScopeHeader *new_scope = new(this) SMemoryScopeHeader;
            new_scope->prevScope = this->currentScope;
            this->currentScope = new_scope;
        }

        // Simply load the current offset & block to return to when scope ends
        this->currentScope->scopeOffset =
                this->currentBlock->offset - sizeof(SMemoryScopeHeader) - sizeof(SMemoryUnitHeader);
        this->currentScope->firstScopeBlock = this->currentBlock;
    }

    void MemoryPool::endScope() {
        // Free all blocks until the start of scope
        while (this->currentBlock != this->currentScope->firstScopeBlock) {
            this->currentBlock = this->currentBlock->prev;
            if (!use_mmap)
                std::free(this->currentBlock->next);
            else
                munmap(this->currentBlock->next, sizeof(SMemoryBlockHeader) + this->currentBlock->next->blockSize);

            this->currentBlock->next = nullptr;
        }

        this->currentBlock->offset = this->currentScope->scopeOffset;
    }
}