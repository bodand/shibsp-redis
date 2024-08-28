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
 * redis-connection.0.cpp
 *
 * Provides support for performing a connection via hiredis version 0.14.1 and
 * above.
 * Limitations compared to 1.0.0:
 *   - no redisOptions struct, only address-port connection possible
 *   - no TLS
 *   - only one timeout value can be configured, after
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
    m_logger.info("connecting to Redis at %s:%d", redisHost.c_str(), redisPort);
    if (config.nonBlocking) {
        m_redis = redisConnectNonBlock(redisHost.c_str(), redisPort);
    } else {
        m_redis = redisConnect(redisHost.c_str(), redisPort);
    }

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

    if (config.commandTimeoutMillisec != 0) {
        m_command_timeout.tv_sec = config.commandTimeoutMillisec / 1000;
        m_command_timeout.tv_usec = config.commandTimeoutMillisec % 1000 * 1000;
        redisSetTimeout(m_redis, m_command_timeout);
    }

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
