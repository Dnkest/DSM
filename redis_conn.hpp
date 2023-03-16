#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <functional>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>

class RedisReply {
public:
    RedisReply(redisReply* reply)
        : m_reply(reply)
    {}
    // RedisReply(const RedisReply&) = delete;
    RedisReply& operator=(const RedisReply&) = delete;

    ~RedisReply() {
        freeReplyObject(m_reply);
    }

    redisReply* operator->() const {
        return m_reply;
    }

private:
    redisReply* m_reply;
};

class RedisConnection {
public:
    RedisConnection(const std::string& hostname, int port)
        : hostname_(hostname), port_(port), context_(nullptr), async_context_(nullptr)
    {
        connect();
    }

    ~RedisConnection() {
        disconnect();
    }

    // Synchronous command execution
    RedisReply executeCommand(const char* format, ...) {
        va_list args;
        va_start(args, format);
        redisReply* reply = (redisReply*)redisvCommand(context_, format, args);
        va_end(args);
        if (reply == nullptr) {
            throw std::runtime_error("Redis command failed");
        }
        return RedisReply(reply);
    }

    // Asynchronous command execution
    void executeCommandAsync(const std::string& command, std::function<void(RedisReply)> callback) {
        std::vector<const char*> argv;
        std::vector<size_t> argvlen;
        std::istringstream iss(command);
        std::string arg;
        while (iss >> arg) {
            argv.push_back(arg.c_str());
            argvlen.push_back(arg.size());
        }
        redisAsyncCommandArgv(async_context_, [](redisAsyncContext* c, void* r, void* privdata) {
            redisReply* reply = (redisReply*)r;
            std::function<void(RedisReply)>* callback = (std::function<void(RedisReply)>*)privdata;
            (*callback)(RedisReply(reply));
            delete callback;
        }, new std::function<void(RedisReply)>(callback), argv.size(), argv.data(), argvlen.data());
    }


    void subscribeToChannel(const std::string& channel, std::function<void(std::string)> callback) {
        async_context_ = redisAsyncConnect(hostname_.c_str(), port_);
        if (async_context_ == nullptr || async_context_->err) {
            throw std::runtime_error("Unable to connect to Redis server (pub/sub)");
        }
        redisAsyncCommand(async_context_, [](redisAsyncContext* c, void* r, void* privdata) {
            redisReply* reply = (redisReply*)r;
            std::function<void(std::string)>* callback = (std::function<void(std::string)>*)privdata;
            (*callback)(std::string(reply->element[2]->str, reply->element[2]->len));
        }, new std::function<void(std::string)>(callback), "SUBSCRIBE %s", channel.c_str());
    }

    void publishToChannel(const std::string& channel, const std::string& message) {
        std::string command = "PUBLISH " + channel + " " + message;
        std::cout << command << std::endl;
        executeCommandAsync(command, [](RedisReply reply){});
    }

private:
    std::string hostname_;
    int port_;
    redisContext* context_;
    redisAsyncContext* async_context_;

    void connect() {
        context_ = redisConnect(hostname_.c_str(), port_);
        if (context_ == nullptr || context_->err) {
            disconnect();
            throw std::runtime_error("Unable to connect to Redis server");
        }
        async_context_ = redisAsyncConnect(hostname_.c_str(), port_);
        if (async_context_ == nullptr || async_context_->err) {
            disconnect();
            throw std::runtime_error("Unable to connect to Redis server (async)");
        }
    }

    void disconnect() {
        if (context_ != nullptr) {
            redisFree(context_);
            context_ = nullptr;
        }
        if (async_context_ != nullptr) {
            redisAsyncDisconnect(async_context_);
            async_context_ = nullptr;
        }
    }
};