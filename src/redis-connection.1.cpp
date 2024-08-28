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
 * redis-connection.1.cpp
 *
 * Provides support for performing a connection via hiredis version 1.0.0 and
 * above.
 */

#include "redis-connection.h"
#include "connection-lost-exception.h"
#include "redirected-exception.h"

// XXX Win32 - special config headers
#include "config.h"

#include <xmltooling/util/XMLHelper.h>
#include <xmltooling/logging.h>

using namespace xmltooling;

void spredis::RedisConnection::connect(const RedisConfig& config,
                                       const std::string& redisHost,
                                       const int redisPort) {
    redisOptions opt{};
    REDIS_OPTIONS_SET_TCP(&opt, redisHost.c_str(), redisPort);
    if (config.commandTimeoutMillisec != 0) {
        m_command_timeout.tv_sec = config.commandTimeoutMillisec / 1000;
        m_command_timeout.tv_usec = config.commandTimeoutMillisec % 1000 * 1000;
        opt.command_timeout = &m_command_timeout;
    }
    if (config.connectTimeoutMillisec != 0) {
        m_connect_timeout.tv_sec = config.connectTimeoutMillisec / 1000;
        m_connect_timeout.tv_usec = config.connectTimeoutMillisec % 1000 * 1000;
        opt.connect_timeout = &m_connect_timeout;
    }

    if (config.nonBlocking) opt.options |= REDIS_OPT_NONBLOCK;

    m_logger.info("connecting to Redis at %s:%d", redisHost.c_str(), redisPort);
    m_redis = redisConnectWithOptions(&opt);
    if (m_redis == NULL)
        // allocation error occured, so we are probably in a really constrainted
        // sitaution, therefore not allocating more to create a string holding
        // the address where we tried to connect to, the allocation required by
        // the exception itself is just enough to hope that it goes through
        // note: this message "should" fit into sso, increasing our chance of
        // successfully reporting what broke
        throw XMLToolingException("!alloc: redis");
    if (m_redis->err) {
        const std::string errorString = std::string("Error initializing Redis connection: ") + m_redis->errstr;
        redisFree(m_redis);
        m_redis = NULL;
        throw ConnectionLostException(errorString);
    }

    // perform TLS handshake if configured
#ifdef SHIBSP_HAVE_HIREDIS_SSL
    if (config.tls) {
        m_logger.info("Performing TLS handshake with host %s:%u",
                      redisHost.c_str(), redisPort);
        redisSSLContextError sslError = REDIS_SSL_CTX_NONE;
        m_ssl = redisCreateSSLContext(
            config.tls.caBundleOrNull(),
            config.tls.caDirectoryOrNull(),
            config.tls.clientCertOrNull(),
            config.tls.clientKeyOrNull(),
            NULL,
            &sslError);
        if (m_ssl == NULL || sslError != REDIS_SSL_CTX_NONE) {
            m_logger.error("Cannot create TLS context while connecting to %s:%u: %s",
                           redisHost.c_str(), redisPort,
                           redisSSLContextGetError(sslError));
            throw XMLToolingException("Cannot create TLS context");
        }

        const int handshook = redisInitiateSSLWithContext(m_redis, m_ssl);
        if (handshook != REDIS_OK) {
            m_logger.error("TLS error during handshake with host %s:%u: %s",
                           redisHost.c_str(), redisPort,
                           m_redis->errstr);
            throw XMLToolingException("Cannot establish TLS connection to host "
                                      + redisHost + ":" + std::to_string(redisPort));
        }
    }
#endif

    // authentication
    switch (config.authScheme()) {
        case RedisConfig::AUTH_DISABLED:
            break;
        case RedisConfig::AUTH_DEFAULT_STYLE:
            m_logger.info("Performing default authentication at host %s:%u",
                          redisHost.c_str(), redisPort);
            RedisReply(this,
                       redisCommand(m_redis, "AUTH %s",
                                    config.authnPassword.c_str()))
                    .throwIfErroneous("ctor", "AUTH");
            break;
        case RedisConfig::AUTH_ACL_STYLE:
            m_logger.info("Performing ACL-style authentication at host %s:%u: user %s",
                          redisHost.c_str(), redisPort, config.authnUsername.c_str());
            RedisReply(this,
                       redisCommand(m_redis, "AUTH %s %s",
                                    config.authnUsername.c_str(),
                                    config.authnPassword.c_str()))
                    .throwIfErroneous("ctor", "AUTH");
            break;
    }
}
