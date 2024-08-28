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
 * redis-store.cpp
 *
 * Storage Service using Redis. This file contains the entry-point
 * for the plugin loader's dynamic loading module.
 */

#if defined (_MSC_VER) || defined(__BORLANDC__)
# include "config_win32.h"
#else
#endif

#ifdef WIN32
# define _CRT_NONSTDC_NO_DEPRECATE 1
# define _CRT_SECURE_NO_DEPRECATE 1
# define MCEXT_EXPORTS __declspec(dllexport)
#else
# define MCEXT_EXPORTS
#endif

#include "common.h"
#include "redis-reply.h"
#include "redis-connection.h"
#include "redis-cluster.h"

#include <hiredis/hiredis.h>

#include <xmltooling/logging.h>
#include <xmltooling/XMLToolingConfig.h>
#include <xmltooling/util/StorageService.h>

using namespace xmltooling;
using namespace xercesc;
using namespace boost;
using namespace std;
using namespace spredis;

namespace {
    // https://redis.io/docs/latest/develop/use/keyspace -> 512 MB
    const unsigned int redisMaxKeySize = 512U * 1000 * 1000;
    const unsigned int redisShibMaxContextSize = redisMaxKeySize / 2 - 1; // context: <- color -1
    const unsigned int redisShibMaxKeySize = redisMaxKeySize / 2 - 2; // context:{prefixkey} <- braces -2

    // https://redis.io/docs/latest/develop/data-types/strings -> 512 MB
    const unsigned int redisMaxValueSize = 512U * 1000 * 1000;

    class RedisStorageService SHIBSP_FINAL : public StorageService {
    public:
        explicit RedisStorageService(Redis* conn);

        const Capabilities& getCapabilities() const {
            return m_capabilities;
        }

        bool createString(const char* context, const char* key, const char* value, time_t expiration);

        int readString(const char* context, const char* key, std::string* pvalue, time_t* pexpiration, int version);

        int updateString(const char* context, const char* key, const char* value, time_t expiration, int version);

        bool deleteString(const char* context, const char* key);

        bool createText(const char* context, const char* key, const char* value, time_t expiration) {
            return createString(context, key, value, expiration);
        }

        int readText(const char* context, const char* key, std::string* pvalue, time_t* pexpiration, int version) {
            return readString(context, key, pvalue, pexpiration, version);
        }

        int updateText(const char* context, const char* key, const char* value, time_t expiration, int version) {
            return updateString(context, key, value, expiration, version);
        }

        bool deleteText(const char* context, const char* key) {
            return deleteString(context, key);
        }

        void reap(const char*) {
            // redis guarantees this to be a nop
        }

        void updateContext(const char* context, time_t expiration);

        void deleteContext(const char* context);

        ~RedisStorageService() { delete m_connection; }

    private:
        Redis* m_connection;
        Capabilities m_capabilities;

        class SetExpirationTo {
        public:
            explicit SetExpirationTo(time_t expirationTo)
                : m_expiration(expirationTo) {
            }

            void
            operator()(RedisConnection* connection, const string& fullKey) const {
                RedisReply reply(connection,
                                 redisCommand(&connection->getRedisContext(),
                                              "EXPIREAT %b %lld",
                                              fullKey.c_str(),
                                              fullKey.size(),
                                              static_cast<long long>(m_expiration)));
                RedisReply reply_version(connection,
                                         redisCommand(&connection->getRedisContext(),
                                                      "EXPIREAT version.of:%b %lld",
                                                      fullKey.c_str(),
                                                      fullKey.size(),
                                                      static_cast<long long>(m_expiration)));
            }

        private:
            time_t m_expiration;
        };


        class Delete {
        public:
            void
            operator()(RedisConnection* connection, const string& fullKey) const {
                RedisReply reply(connection,
                                 redisCommand(&connection->getRedisContext(),
                                              "UNLINK %b version.of:%b",
                                              fullKey.c_str(),
                                              fullKey.size(),
                                              fullKey.c_str(),
                                              fullKey.size()));
            }
        };
    };


    RedisStorageService::RedisStorageService(Redis* conn)
        : m_connection(conn),
          m_capabilities(redisShibMaxContextSize,
                         redisShibMaxKeySize - m_connection->getPrefix().size(),
                         redisMaxValueSize) {
    }

    bool RedisStorageService::createString(const char* context, const char* key, const char* value, time_t expiration) {
        const StorageId id = m_connection->make_id(context, key);
        return m_connection->set(id, value, expiration);
    }

    int RedisStorageService::readString(const char* context, const char* key, std::string* pvalue, time_t* pexpiration,
                                        int version) {
        const StorageId id = m_connection->make_id(context, key);
        if (version > 0) return m_connection->getVersioned(id, pvalue, pexpiration, version);
        return m_connection->forceGet(id, pvalue, pexpiration);
    }

    int RedisStorageService::updateString(const char* context, const char* key, const char* value, time_t expiration,
                                          int version) {
        const StorageId id = m_connection->make_id(context, key);
        if (version > 0) return m_connection->updateVersioned(id, value, expiration, version);
        return m_connection->forceUpdate(id, value, expiration);
    }

    bool RedisStorageService::deleteString(const char* context, const char* key) {
        const StorageId id = m_connection->make_id(context, key);
        return m_connection->remove(id);
    }

    void RedisStorageService::updateContext(const char* context, time_t expiration) {
        m_connection->scanContext(context, SetExpirationTo(expiration));
    }

    void RedisStorageService::deleteContext(const char* context) {
        m_connection->scanContext(context, Delete());
    }

    StorageService* RedisStorageServiceFactory(const DOMElement* const & e, bool) {
        const RedisConfig config(e);
        return config.clustered()
                   ? new RedisStorageService(new RedisCluster(config))
                   : new RedisStorageService(new RedisConnection(config));
    }
}

extern "C" int MCEXT_EXPORTS xmltooling_extension_init(void*) {
    // Register this SS type
    XMLToolingConfig::getConfig().StorageServiceManager.registerFactory("REDIS", RedisStorageServiceFactory);
    return 0;
}

extern "C" void MCEXT_EXPORTS xmltooling_extension_term() {
    XMLToolingConfig::getConfig().StorageServiceManager.deregisterFactory("REDIS");
}
