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
 * redis-connection.h
 *
 * Provides the RedisConnection class, representing a single connection instance
 * to a Redis server.
 */

#ifndef REDIS_CONNECTION_H
#define REDIS_CONNECTION_H

#include <cassert>
#include <string>

#include "common.h"
#include "cluster-range.h"
#include "redis.h"
#include "redis-reply.h"

// XXX Win32 - special config headers
#include "config.h"

#include <hiredis/hiredis.h>
#include <xmltooling/util/Threads.h>
#include <xmltooling/logging.h>

#ifdef SHIBSP_HAVE_HIREDIS_SSL
#include <hiredis/hiredis_ssl.h>
#endif

namespace spredis {
    class SHIBSP_HIDDEN RedisConnection SHIBSP_FINAL : public Redis {
    public:
        void connect(const RedisConfig& config, const std::string& redisHost, int redisPort);

        RedisConnection(const RedisConfig& config);

        RedisConnection(const RedisConfig& config,
                        const std::string& ovHost,
                        int ovPort);

        ~RedisConnection() {
#ifdef SHIBSP_HAVE_HIREDIS_SSL
            redisFreeSSLContext(m_ssl);
#endif
            redisFree(m_redis);
        }

        redisContext& getRedisContext() { return *m_redis; }
        const redisContext& getRedisContext() const { return *m_redis; }

        bool set(const StorageId& id, const char* value, time_t expiration);

        int getVersioned(const StorageId& id, std::string* out_value, time_t* out_expiration, int minVersion);

        int forceGet(const StorageId& id, std::string* out_value, time_t* out_expiration);

        int updateVersioned(const StorageId& id, const char* value, time_t expiration, int ifVersion);

        int forceUpdate(const StorageId& id, const char* value, time_t expiration);

        bool remove(const StorageId& id);

        void handleCriticalError(const char* fn, int recurse = 0);

        void handlePotentialMovedError(const std::string& err_str) const;

        void handleCommandError(const char* fn, const char* command, const char* errorBuf, size_t errorLen) const;

        template<class Fn>
        void iterateSlots(Fn callback) {
            const RedisReply reply(this, redisCommand(m_redis, "CLUSTER SLOTS"));
            reply.ensureType(REDIS_REPLY_ARRAY, "iterateSlots");

            for (size_t i = 0; i < reply->elements; ++i) {
                const RedisReply rangeEntry(this, reply->element[i], RedisReply::nonOwning);
                rangeEntry.ensureType(REDIS_REPLY_ARRAY, "iterateSlots");

                if (rangeEntry->elements < 3) {
                    m_logger.error(
                        "Invalid slots configuration returned from redis: "
                        "slot-range is missing nodes");
                    throw xmltooling::IOException("Fatal error self-configuring Redis cluster");
                }

                // casts, while narrowing, are safe here, these are values are
                // 0 <= x < 16384, which means two bytes traveling in 8 bytes'
                // space and int is guaranteed to allow storing of two bytes,
                // even on uncommon hw configurations
                const ClusterRange<> range(static_cast<int>(rangeEntry->element[0]->integer),
                                           static_cast<int>(rangeEntry->element[1]->integer));

                const RedisReply nodeReply(this, rangeEntry->element[2], RedisReply::nonOwning);
                nodeReply.ensureType(REDIS_REPLY_ARRAY, "iterateSlots");

                if (rangeEntry->elements < 2) {
                    m_logger.error(
                        "Invalid slots configuration returned from redis: "
                        "slot-range's node is missing ip and port data");
                    throw xmltooling::IOException("Fatal error self-configuring Redis cluster");
                }

                // narrowing fine again, port value is <65535, which is 2 bytes unsigned,
                // port type guaranteed to be able to hold it
                const ClusterNode node(std::string(nodeReply->element[0]->str, nodeReply->element[0]->len),
                                       static_cast<unsigned short>(nodeReply->element[1]->integer));

                callback(range, node);
            }
        }

    protected:
        size_t scanContextTypeless(const char* context, RawCallbackType callback, void* callbackContext) override;

    private:
        struct private_tag_t {
        };

        // ~"primary constructor" constructs the object properties, but does not
        // connect, the public constructos perform the actual connection, as
        // they need to
        // -> all other constructors should direct to this, then call connect
        RedisConnection(const RedisConfig& config, private_tag_t /* disambiguate */);

        int getOnlyVersion(const StorageId& id);

        int parseNumber(const StorageId& id, const char* fn, const char* str, size_t len) const;;

        void appendCommand(const char* fmt, ...) const;

        void recreateContext(int recurse = 0);

        RedisConnection(const RedisConnection&)
            : Redis("disable copy"),
              m_logger(log4shib::Category::getInstance("")) { assert(false); }

        RedisConnection&
        operator=(const RedisConnection&) {
            assert(false);
            return *this;
        }

        redisContext* m_redis;
        timeval m_command_timeout;
        timeval m_connect_timeout;
        xmltooling::logging::Category& m_logger;
        boost::scoped_ptr<xmltooling::Mutex> m_mutex;
#ifdef SHIBSP_HAVE_HIREDIS_SSL
        redisSSLContext* m_ssl;
#endif
    };
}


#endif //REDIS_CONNECTION_H
