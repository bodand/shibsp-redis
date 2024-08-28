/**
 * Licensed to the University Corporation for Advanced Internet
 * Development, Inc. (UCAID) under one or more contributor license
 * agreements. See the NOTICE file distributed with this work for
 * additional information regarding copyright ownership.
 *
 * UCAID licenses this file to you under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the
 * License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 */

/**
 * redis.h
 *
 * Provides the RedisReply RAII wrapper class.
 */

#ifndef REDIS_REPLY_H
#define REDIS_REPLY_H

#include <cassert>

#include "common.h"
#include <hiredis/hiredis.h>

namespace spredis {
    class RedisConnection;

    class SHIBSP_HIDDEN RedisReply SHIBSP_FINAL {
    public:
        static const bool nonOwning;

        explicit RedisReply(RedisConnection* connection, void* buffer = NULL, bool owning = true);

        ~RedisReply() {
            resetReply();
        }

        void throwIfErroneous(const char* fn, const char* command) const;

        void getNextFromConnection(const char* fn, const char* command, int type = 0);

        const redisReply* operator->() const { return m_reply; }
        redisReply* operator->() { return m_reply; }

        void ensureType(int type, const char* fn) const;

    private:
        void resetReply() {
            if (m_owning && m_reply) freeReplyObject(m_reply);
            m_reply = NULL;
        }

        RedisReply(const RedisReply&) { assert(false); }

        RedisReply&
        operator=(const RedisReply&) {
            assert(false);
            return *this;
        }

        bool m_owning;
        RedisConnection* m_connection;
        redisReply* m_reply;
    };
}

#endif //REDIS_REPLY_H
