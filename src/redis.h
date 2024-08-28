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
 * Provides the Redis abstract class, to be used as a base for single-instance
 * and clustered modes.
 */

#ifndef REDIS_H
#define REDIS_H

#include <string>
#include <vector>

#include <xercesc/dom/DOMElement.hpp>
#include <xmltooling/util/XMLHelper.h>

#include "cluster-node.h"
#include "storage-id.h"
#include "common.h"

namespace spredis {
    class RedisConnection;

    class SHIBSP_HIDDEN RedisTlsConfig SHIBSP_FINAL {
    public:
        const bool enabled;
        const std::string clientCert;
        const std::string clientKey;
        const std::string caBundle;
        const std::string caDirectory;

        explicit RedisTlsConfig(const xercesc::DOMElement* e);

        const char* clientCertOrNull() const {
            if (clientCert.empty()) return NULL;
            return clientCert.c_str();
        }

        const char* clientKeyOrNull() const {
            if (clientKey.empty()) return NULL;
            return clientKey.c_str();
        }

        const char* caBundleOrNull() const {
            if (caBundle.empty()) return NULL;
            return caBundle.c_str();
        }

        const char* caDirectoryOrNull() const {
            if (caDirectory.empty()) return NULL;
            return caDirectory.c_str();
        }

        explicit operator bool() const { return enabled; }
    };

    class SHIBSP_HIDDEN RedisConfig SHIBSP_FINAL {
    public:
        enum AuthStyle {
            AUTH_DISABLED,
            AUTH_DEFAULT_STYLE,
            AUTH_ACL_STYLE
        };

        const std::string host;
        const unsigned short port;
        const std::string prefix;
        const std::vector<ClusterNode> initialNodes;
        const int connectTimeoutMillisec;
        const int commandTimeoutMillisec;
        const bool nonBlocking;
        const std::string authnUsername;
        const std::string authnPassword;
        const unsigned int maxRetries;
        const unsigned int baseWait;
        const unsigned int maxWait;
        const RedisTlsConfig tls;

        explicit RedisConfig(const xercesc::DOMElement* e);

        bool clustered() const { return !initialNodes.empty(); }

        AuthStyle authScheme() const {
            if (authnPassword.empty()) return AUTH_DISABLED;
            if (authnUsername.empty()) return AUTH_DEFAULT_STYLE;
            return AUTH_ACL_STYLE;
        }
    };

    /**
     * Abstract baseclass of the Redis connection hiearchy. Provides the main
     * functionality required to perform the storage plugin tasks.
     */
    class SHIBSP_HIDDEN Redis {
        const std::string m_prefix;

    public:
        explicit Redis(const std::string& m_prefix)
            : m_prefix(m_prefix) {
        }

        const std::string& getPrefix() const { return m_prefix; }

        StorageId make_id(const char* const context,
                          const char* const key) const {
            return StorageId(context, key, m_prefix.c_str());
        }

        virtual bool set(const StorageId& id, const char* value, time_t expiration) = 0;

        virtual int getVersioned(const StorageId& id, std::string* out_value, time_t* out_expiration,
                                 int minVersion) = 0;

        virtual int forceGet(const StorageId& id, std::string* out_value, time_t* out_expiration) = 0;

        virtual int updateVersioned(const StorageId& id, const char* value, time_t expiration, int ifVersion) = 0;

        virtual int forceUpdate(const StorageId& id, const char* value, time_t expiration) = 0;

        virtual bool remove(const StorageId& id) = 0;

        template<class Fn>
        void scanContext(const char* context, Fn callback) {
            scanContextTypeless(context, RawCallback<Fn>, static_cast<void*>(&callback));
        }

        virtual ~Redis() {
        }

    private:
        template<class Fn>
        static void RawCallback(void* fn, RedisConnection* connection, const std::string& data) {
            (*static_cast<Fn*>(fn))(connection, data);
        }

    protected:
        typedef void (*RawCallbackType)(void*, RedisConnection*, const std::string&);

        virtual size_t scanContextTypeless(const char* context, RawCallbackType callback, void* callbackContext) = 0;
    };
}

#endif //REDIS_H
