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
 * redis-crc-16.cpp
 *
 * Implementation the RedisReply RAII wrapper class.
 */

#include "redis-reply.h"
#include "redis-connection.h"

const bool spredis::RedisReply::nonOwning = false;

spredis::RedisReply::RedisReply(RedisConnection* connection, void* buffer, bool owning)
    : m_owning(owning),
      m_connection(connection),
      m_reply(static_cast<redisReply*>(buffer)) {
}

void spredis::RedisReply::throwIfErroneous(const char* fn, const char* command) const {
    if (m_reply == NULL) m_connection->handleCriticalError(fn);

    assert(m_reply != NULL);
    if (m_reply->type == REDIS_REPLY_ERROR)
        m_connection->handleCommandError(
            fn, command, m_reply->str, m_reply->len);
}

void spredis::RedisReply::getNextFromConnection(const char* fn, const char* command, int type) {
    if (m_reply) resetReply();
    const int status = redisGetReply(&m_connection->getRedisContext(), reinterpret_cast<void**>(&m_reply));
    if (status != REDIS_OK) return m_connection->handleCriticalError(fn);

    throwIfErroneous(fn, command);
    if (type != 0) ensureType(type, fn);
}

void spredis::RedisReply::ensureType(int type, const char* fn) const {
    assert(m_reply && "ensureType called on incomplete reply");

    if (type == m_reply->type) return;
    throw xmltooling::IOException("(" + std::string(fn) + ") incorrect response from Redis server: expected type `" +
                                  std::to_string(type) + "' but got `" + std::to_string(m_reply->type) + "'");
}
