#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <memory>
#include <functional>
#include <sys/mman.h>
#include <signal.h>
#include "redis_conn.hpp"

#define PAGE_SIZE 4096

/*
TODO 
1. add logger
*/


class DSM {
public:
    DSM(const std::string& identifier) : identifier_(identifier), base_(nullptr), size_(0) {
        /*
        TODO: make this a singleton class
        */

        // init redis_conn
        try {
            redis_conn_ = std::make_unique<RedisConnection>("127.0.0.1", 6379);
            // Check if the identifier already exists in Redis
            const auto& reply = redis_conn_->executeCommand("EXISTS %s", identifier_.c_str());
            if (reply->integer != 0) {

            } else {
                // Register the address space in Redis
                redis_conn_->executeCommand("HSET %s size 0", identifier_.c_str());
            }

        } catch (...) {
            // In case of exception, ensure proper cleanup before re-throwing
            redis_conn_.reset(nullptr);
            throw;
        }

        instance = this;
    }

    ~DSM() {
        try {
            redis_conn_->executeCommand("DEL %s", identifier_.c_str());
        } catch (...) {
            // In case of exception, ensure proper cleanup before re-throwing
            redis_conn_.reset(nullptr);
        }
        if (base_ != nullptr) {
            munmap(base_, size_);
            base_ = nullptr;
            size_ = 0;
        }
    }

    void* dsm_malloc(size_t size) {
        if (base_ != nullptr) {
            throw std::runtime_error("Error: dsm_malloc called twice");
        }

        try {
            // Check if base address has been set by another node
            const auto& reply = redis_conn_->executeCommand("HGET %s size", identifier_.c_str());
            if (reply->type == REDIS_REPLY_STRING && std::string(reply->str) == "1") {
                // Base address already exists, return it
                size_ = std::strtoull(reply->str, nullptr, 10);
            } else {
                size_ = size;
                redis_conn_->executeCommand("HSET %s size %lu", identifier_.c_str(), size);
            }
            // create a new block of memory with size {size}
            base_ = mmap(nullptr, size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (base_ == MAP_FAILED) {
                throw std::runtime_error("Failed to allocate virtual memory");
            }

            // Set the signal handler for SIGSEGV
            struct sigaction sa;
            sa.sa_sigaction = static_page_fault_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_SIGINFO;
            if (sigaction(SIGSEGV, &sa, NULL) == -1) {
                munmap(base_, size_);
                throw std::runtime_error("Error: Failed to register signal handler for SIGSEGV");
            }

            // every node is subscribing to the default channel
            // TODO a channel for each identifier
            redis_conn_->subscribeToChannel("default", [this](std::string message) {
                this->writeInvalidationHandler(message);
            });
        } catch (const std::exception& ex) {
            std::cerr << "dsm_malloc failed: " << ex.what() << std::endl;
        }
        return base_;
    }

    // TODO
    void dsm_barrier() {}

    static void static_page_fault_handler(int signum, siginfo_t* info, void* context) { instance->page_fault_handler(signum, info, context); }

    void page_fault_handler(int signum, siginfo_t* info, void* context) {

        void* fault_addr = info->si_addr;
        ucontext_t* ucontext = (ucontext_t*)context;
        unsigned int error_code = ucontext->uc_mcontext.gregs[REG_ERR];

        void* page_addr = (void*)((uintptr_t)info->si_addr & ~(PAGE_SIZE - 1));

        if (error_code & (1 << 1)) {
            // write fault, do write invalidation

            // TODO: Publish a message to notify other nodes
            // more specifically the offset to the page alligned address
            std::stringstream ss;
            ss << std::hex << page_addr;
            redis_conn_->publishToChannel("default", ss.str().c_str());

            // TODO: copy the page to redis

        } else {
            // read fault, request the page from redis

            // TODO: copy the page from redis

            // TODO: make this region read-only
            mprotect(page_addr, PAGE_SIZE, PROT_READ);
        }
    }

    void writeInvalidationHandler(std::string message) {
        // got write invalidation

        // TODO: revoke the read access from this region
    }


private:
    std::string identifier_;
    std::unique_ptr<RedisConnection> redis_conn_;
    void* base_;
    size_t size_;
    static DSM* instance;
};
DSM* DSM::instance = nullptr;

