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
 * redis-connection.cpp
 *
 * Implementation of the single-instance communication to a Redis server.
 */

#include "redis-connection.h"
#include "connection-lost-exception.h"
#include "redirected-exception.h"

// XXX Win32 - special config headers
#include "config.h"

#include <xmltooling/util/XMLHelper.h>
#include <xmltooling/logging.h>

using namespace xmltooling;

namespace {
    const int optimisticConcurrencyRetryCount = 3;
    const int connectionRetryCount = 3;
}

spredis::RedisConnection::RedisConnection(const RedisConfig& config) : RedisConnection(config, private_tag_t()) {
    const std::string& redisHost = config.host;
    const int redisPort = config.port;

    connect(config, redisHost, redisPort);
}

spredis::RedisConnection::RedisConnection(const RedisConfig& config, private_tag_t dispatcher)
    : Redis(config.prefix),
      m_redis(NULL),
      m_command_timeout(),
      m_connect_timeout(),
      m_logger(logging::Category::getInstance("XMLTooling.StorageService.REDIS")),
      m_mutex(Mutex::create())
#ifdef SHIBSP_HAVE_HIREDIS_SSL
    , m_ssl(NULL)
#endif
{
    (void) dispatcher;
}

spredis::RedisConnection::RedisConnection(const RedisConfig& config,
                                          const std::string& ovHost,
                                          const int ovPort) : RedisConnection(config, private_tag_t()) {
    connect(config, ovHost, ovPort);
}

void spredis::RedisConnection::handleCriticalError(const char* fn, int recurse) {
    m_logger.crit("error during Redis communications: %s", m_redis->errstr);

    // try n-times to reconnect without actual errors, lost connecection could
    // have been a fluke
    if (recurse < connectionRetryCount) recreateContext(recurse + 1);

    // connection is actually lost, create error msg and throw specific error,
    // thus allowing cluster mode to catch it and initiate a reconfiguration,
    // and single-instance mode can catch it outside and deal with it how it can
    const std::string msg = std::string("RedisConnection::") + fn + ": " + m_redis->errstr;
    throw ConnectionLostException(msg);
}

void spredis::RedisConnection::handlePotentialMovedError(const std::string& err_str) const {
    if (err_str.substr(0, 6) != "MOVED ") return;

    m_logger.warn("Redis cluster configuration changed: reconfiguring caused by error: " + err_str);

    // WARNING! The following code is heavy with string indexing and prone to
    //          off-by-one errors, proceed with caution and a debugger
    const size_t spaceBeforeLocation = err_str.find(' ', 7);
    const size_t portColonLocation = err_str.find(':', spaceBeforeLocation);

    // parse number
    unsigned int port = 6379;
    try {
        // need to parse into unsigned long because stoui doesn't exist,
        // and if sizeof(int) == 2 (which is completely possible), a normal int
        // cannot hold all port values properly and inadvertedly trigger the
        // overflow error branch below
        port = std::stoul(err_str.substr(portColonLocation + 1));
    } catch (const std::invalid_argument&) {
        m_logger.crit("(handleCommandError) catastrophic cascading error: "
            "value sent as port value is not an integer, trying 6379");
    } catch (const std::out_of_range&) {
        m_logger.crit("(handleCommandError) catastrophic cascading error: "
            "value sent as port value exceeds integer limit of unsigned long, trying 6379");
    }

    throw RedirectedException(err_str.substr(spaceBeforeLocation + 1,
                                             portColonLocation - spaceBeforeLocation - 1),
                              port);
}

void spredis::RedisConnection::handleCommandError(const char* fn, const char* command, const char* errorBuf,
                                                  size_t errorLen) const {
    const std::string err_str(errorBuf, errorLen);
    m_logger.error("execution of Redis command failed: %s: %.*s", command, errorLen, errorBuf);

    if (err_str.length() > sizeof("CLUSTERDO")) {
        // the cluster is currently down, notify things via losing the connection
        // maybe it fixes itself and the failure is not fatal
        throw ConnectionLostException("CLUSTERDOWN received: Redis cluster is unavailable at the moment");
    }

    // jump to redirection handling if error has the potential to actually be a
    // redirection and not a "true error"
    if (err_str.length() > sizeof("MOVED")) handlePotentialMovedError(err_str);

    // generic error that the plugin doesn't know how to handle
    const std::string msg = std::string("RedisConnection::") + fn + ": " + command + ": " + err_str;
    throw IOException(msg);
}

size_t spredis::RedisConnection::scanContextTypeless(const char* context,
                                                     RawCallbackType callback, void* callbackContext) {
    const Lock lck(m_mutex);
    size_t count = 0;
    RedisReply reply(this, NULL);
    int nextScanState = 0;

    for (;;) {
        appendCommand("SCAN %d MATCH %s:*", nextScanState, context);
        reply.getNextFromConnection("scanContext", "SCAN", REDIS_REPLY_ARRAY);

        if (reply->elements != 2)
            return count;

        if (reply->element[0]->type != REDIS_REPLY_STRING
            && reply->element[1]->type != REDIS_REPLY_ARRAY)
            return count;


        redisReply** const elems = reply->element[1]->element;
        const size_t elems_sz = reply->element[1]->elements;
        count += elems_sz;

        for (size_t i = 0; i < elems_sz; ++i) {
            if (elems[i]->type != REDIS_REPLY_STRING) {
                m_logger.warn("(scanContext) non-string element returned during scanning: type %d at index %zu",
                              elems[i]->type,
                              i);
                continue;
            }
            callback(callbackContext, this, std::string(elems[i]->str, elems[i]->len));
        }

        try {
            nextScanState = stoi(std::string(reply->element[0]->str, reply->element[0]->len));
            if (nextScanState == 0) break;
        } catch (const std::invalid_argument&) { break; }
        catch (const std::out_of_range&) { break; }
    }

    return count;
}

bool spredis::RedisConnection::set(const StorageId& id, const char* value, const time_t expiration) {
    m_logger.debug("(set) setting key " SPREDIS_SID_FMT "@1 (exp: %lld)", SPREDIS_SID_FPARAM(id),
                   static_cast<long long>(expiration));

    const Lock ulock(m_mutex);

    // load tr. as pipelined commands
    appendCommand("MULTI");
    appendCommand("SET " SPREDIS_SID_FMT " %s NX EXAT %lld",
                  SPREDIS_SID_FPARAM(id),
                  value,
                  static_cast<long long>(expiration));
    appendCommand("SET version.of:" SPREDIS_SID_FMT " 1 NX EXAT %lld",
                  SPREDIS_SID_FPARAM(id),
                  static_cast<long long>(expiration));
    appendCommand("EXEC");

    // get pipeline results
    RedisReply reply(this);
    reply.getNextFromConnection("set", "MULTI", REDIS_REPLY_STATUS);
    reply.getNextFromConnection("set", "SET (data)", REDIS_REPLY_STATUS);
    reply.getNextFromConnection("set", "SET (version)", REDIS_REPLY_STATUS);
    reply.getNextFromConnection("set", "EXEC", REDIS_REPLY_ARRAY); // perform tr.

    if (reply->elements != 2) // incorrect amount of results is a fatal error
        handleCommandError("set", "EXEC",
                           "incorrect amount of results from EXEC",
                           sizeof("incorrect amount of results from EXEC") - 1);

    if (reply->element[0]->type != REDIS_REPLY_STATUS) {
        // nil if NX caused a failure in insertion i.e. the key already exists
        if (reply->element[0]->type == REDIS_REPLY_NIL) return false;
        handleCommandError("set", "SET (data)",
                           reply->element[0]->str, reply->element[0]->len);
    }
    if (reply->element[1]->type != REDIS_REPLY_STATUS) {
        // nil if NX caused a failure in insertion i.e. the key already exists
        if (reply->element[1]->type == REDIS_REPLY_NIL) {
            m_logger.warn("version value exists for non-existent key " SPREDIS_SID_FMT,
                          SPREDIS_SID_FPARAM(id));
            // clean up value and version
            RedisReply tmp(this,
                           redisCommand(m_redis, "UNLINK " SPREDIS_SID_FMT " version.of:" SPREDIS_SID_FMT,
                                        SPREDIS_SID_FPARAM(id),
                                        SPREDIS_SID_FPARAM(id)));
            return false;
        }

        handleCommandError("set", "SET (version)",
                           reply->element[1]->str, reply->element[1]->len);
    }

    return true;
}

int spredis::RedisConnection::getVersioned(const StorageId& id, std::string* out_value, time_t* out_expiration,
                                           int minVersion) {
    m_logger.debug("(getVersioned) getting key " SPREDIS_SID_FMT "@%d+", SPREDIS_SID_FPARAM(id),
                   minVersion);
    RedisReply reply(this);
    const Lock ulock(m_mutex);

    // if no out parameter is present, don't actually perform any lookups
    if (out_value == NULL && out_expiration == NULL) return getOnlyVersion(id);

    for (int tryCount = 0; tryCount < optimisticConcurrencyRetryCount; ++tryCount) {
        appendCommand("WATCH version.of:" SPREDIS_SID_FMT,
                      SPREDIS_SID_FPARAM(id));
        reply.getNextFromConnection("getVersioned", "WATCH", REDIS_REPLY_STATUS);

        // if version mismatch, set out_value to null, so the below code is the
        // same, regardless of the reason we don't need to read the value data
        const int currentVersion = getOnlyVersion(id);
        if (currentVersion < minVersion) out_value = NULL;

        // if no out parameter is present after changes, just return the version
        // after UNWATCH-ing
        if (out_value == NULL && out_expiration == NULL) {
            appendCommand("UNWATCH");
            reply.getNextFromConnection("getVersioned", "UNWATCH", REDIS_REPLY_STATUS);
            return currentVersion;
        }

        // read tr. as pipeline
        appendCommand("MULTI");
        if (out_value) appendCommand("GET " SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(id));
        if (out_expiration) appendCommand("EXPIRETIME " SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(id));
        appendCommand("EXEC");

        reply.getNextFromConnection("getVersioned", "MULTI", REDIS_REPLY_STATUS);
        if (out_value) reply.getNextFromConnection("getVersioned", "GET (data)", REDIS_REPLY_STATUS);
        if (out_expiration) reply.getNextFromConnection("getVersioned", "EXPIRETIME", REDIS_REPLY_STATUS);
        reply.getNextFromConnection("getVersioned", "EXEC");

        if (reply->type == REDIS_REPLY_NIL) {
            m_logger.notice("(getVersioned) concurrency failure: retrying accessing " SPREDIS_SID_FMT,
                            SPREDIS_SID_FPARAM(id));
            continue;
        }
        reply.ensureType(REDIS_REPLY_ARRAY, "getVersioned");

        // incorrect amount of results is a fatal error
        if (reply->elements != 0ULL + (out_value != NULL) + (out_expiration != NULL))
            handleCommandError("getVersioned", "EXEC",
                               "incorrect amount of results from EXEC",
                               sizeof("incorrect amount of results from EXEC") - 1);

        int expiration_index = 0;
        if (out_value) {
            // if out_value is present, expiration is only the 2nd returned value,
            // this trickery allows not repeating the expiration setting code
            ++expiration_index;

            RedisReply(this, reply->element[0], RedisReply::nonOwning).ensureType(REDIS_REPLY_STRING, "getVersioned");
            *out_value = std::string(reply->element[0]->str, reply->element[0]->len);
        }
        if (out_expiration) {
            RedisReply(this, reply->element[expiration_index], RedisReply::nonOwning)
                    .ensureType(REDIS_REPLY_INTEGER, "getVersioned");
            *out_expiration = reply->element[expiration_index]->integer;
        }

        return currentVersion;
    }

    m_logger.warn("(getVersioned) concurrency failure: too-many retries while reading " SPREDIS_SID_FMT,
                  SPREDIS_SID_FPARAM(id));
    return 0;
}

int spredis::RedisConnection::getOnlyVersion(const StorageId& id) {
    m_logger.debug("(getOnlyVersion) short-circuiting to only reading version for key " SPREDIS_SID_FMT "@?",
                   SPREDIS_SID_FPARAM(id));

    appendCommand("GET version.of:" SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(id));
    RedisReply reply(this);
    reply.getNextFromConnection("getOnlyVersion", "GET (version)", REDIS_REPLY_STRING);

    return parseNumber(id, "getOnlyVersion", reply->str, reply->len);
}

int spredis::RedisConnection::parseNumber(const StorageId& id, const char* fn, const char* str,
                                          const size_t len) const
try {
    return stoi(std::string(str, len));
} catch (const std::invalid_argument&) {
    m_logger.error("(%s) non-integer value in version key `version.of:" SPREDIS_SID_FMT "'",
                   fn,
                   SPREDIS_SID_FPARAM(id));
    return 0;
} catch (const std::out_of_range&) {
    m_logger.error("(%s) value in version key `version.of:" SPREDIS_SID_FMT "' exceeds integer limit",
                   fn,
                   SPREDIS_SID_FPARAM(id));
    return 0;
}

int spredis::RedisConnection::forceGet(const StorageId& id, std::string* out_value, time_t* out_expiration) {
    m_logger.debug("(forceGet) getting key " SPREDIS_SID_FMT "@?", SPREDIS_SID_FPARAM(id));
    RedisReply reply(this);
    const Lock ulock(m_mutex);

    // read tr. as pipeline
    appendCommand("MULTI");
    appendCommand("GET version.of:" SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(id));
    if (out_value) appendCommand("GET " SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(id));
    if (out_expiration) appendCommand("EXPIRETIME " SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(id));
    appendCommand("EXEC");

    // exec transaction
    reply.getNextFromConnection("forceGet", "MULTI", REDIS_REPLY_STATUS);
    reply.getNextFromConnection("forceGet", "GET (version)", REDIS_REPLY_STATUS);
    if (out_value) reply.getNextFromConnection("forceGet", "GET (data)", REDIS_REPLY_STATUS);
    if (out_expiration) reply.getNextFromConnection("forceGet", "EXPIRETIME", REDIS_REPLY_STATUS);
    reply.getNextFromConnection("forceGet", "EXEC", REDIS_REPLY_ARRAY);

    // incorrect amount of results is a fatal error
    // version + optional outputs
    if (reply->elements != 1ULL + (out_value != NULL) + (out_expiration != NULL))
        handleCommandError("forceGet", "EXEC",
                           "incorrect amount of results from EXEC",
                           sizeof("incorrect amount of results from EXEC") - 1);

    if (reply->element[0]->type == REDIS_REPLY_NIL
        || reply->element[1]->type == REDIS_REPLY_NIL)
        return 0; // key not found

    RedisReply(this, reply->element[0], RedisReply::nonOwning).ensureType(REDIS_REPLY_STRING, "forceGet");
    const int version = parseNumber(id, "forceGet", reply->element[0]->str, reply->element[0]->len);

    int expiration_index = 1;
    if (out_value) {
        // if out_value is present, expiration is only the 3rd returned value,
        // this trickery allows not repeating the expiration setting code
        ++expiration_index;

        RedisReply(this, reply->element[1], RedisReply::nonOwning).ensureType(REDIS_REPLY_STRING, "forceGet");
        *out_value = std::string(reply->element[1]->str, reply->element[1]->len);
    }
    if (out_expiration) {
        RedisReply(this, reply->element[expiration_index], RedisReply::nonOwning).ensureType(
            REDIS_REPLY_INTEGER, "forceGet");
        *out_expiration = reply->element[expiration_index]->integer;
    }

    return version;
}

int spredis::RedisConnection::updateVersioned(const StorageId& id,
                                              const char* value,
                                              const time_t expiration,
                                              const int ifVersion) {
    m_logger.debug("(upateVersioned) updating key " SPREDIS_SID_FMT "@%d+ (exp: %lld)", SPREDIS_SID_FPARAM(id),
                   ifVersion,
                   static_cast<long long>(expiration));
    RedisReply reply(this);
    const Lock ulock(m_mutex);

    for (int tryCount = 0; tryCount < optimisticConcurrencyRetryCount; ++tryCount) {
        appendCommand("WATCH version.of:" SPREDIS_SID_FMT,
                      SPREDIS_SID_FPARAM(id));
        reply.getNextFromConnection("getVersioned", "WATCH", REDIS_REPLY_STATUS);

        // if version mismatch, report failure
        const int currentVersion = getOnlyVersion(id);
        if (currentVersion != ifVersion) return -1;

        // read tr. as pipeline
        appendCommand("MULTI");
        appendCommand("SET " SPREDIS_SID_FMT " %s XX KEEPTTL", SPREDIS_SID_FPARAM(id), value);
        appendCommand("INCR version.of:" SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(id));
        if (expiration != 0) {
            appendCommand("EXPIREAT " SPREDIS_SID_FMT " %lld",
                          SPREDIS_SID_FPARAM(id),
                          static_cast<long long>(expiration));
            appendCommand("EXPIREAT version.of:" SPREDIS_SID_FMT " %lld",
                          SPREDIS_SID_FPARAM(id),
                          static_cast<long long>(expiration));
        }
        appendCommand("EXEC");

        reply.getNextFromConnection("updateVersioned", "MULTI", REDIS_REPLY_STATUS);
        reply.getNextFromConnection("updateVersioned", "SET (data)", REDIS_REPLY_STATUS);
        reply.getNextFromConnection("updateVersioned", "INCR (version)", REDIS_REPLY_STATUS);
        if (expiration != 0) reply.getNextFromConnection("updateVersioned", "EXPIREAT (data)", REDIS_REPLY_STATUS);
        if (expiration != 0) reply.getNextFromConnection("updateVersioned", "EXPIREAT (version)", REDIS_REPLY_STATUS);
        reply.getNextFromConnection("updateVersioned", "EXEC");

        if (reply->type == REDIS_REPLY_NIL) {
            m_logger.notice("(updateVersioned) concurrency failure: retrying accessing " SPREDIS_SID_FMT,
                            SPREDIS_SID_FPARAM(id));
            continue;
        }
        reply.ensureType(REDIS_REPLY_ARRAY, "getVersioned");

        // incorrect amount of results is a fatal error
        // value + version + 2 * optional expiration
        if (reply->elements != 2 + 2 * (expiration != 0))
            handleCommandError("getVersioned", "EXEC",
                               "incorrect amount of results from EXEC",
                               sizeof("incorrect amount of results from EXEC") - 1);

        RedisReply(this, reply->element[0], RedisReply::nonOwning)
                .ensureType(REDIS_REPLY_STATUS, "updateVersioned");

        const RedisReply incr(this, reply->element[1], RedisReply::nonOwning);
        incr.ensureType(REDIS_REPLY_INTEGER, "updateVersioned");
        if (incr->integer - 1 != currentVersion) {
            m_logger.warn("(updateVersioned) severe concurrency failure: retrying accessing " SPREDIS_SID_FMT,
                          SPREDIS_SID_FPARAM(id));
            continue;
        }

        if (expiration != 0) {
            RedisReply(this, reply->element[2], RedisReply::nonOwning)
                    .ensureType(REDIS_REPLY_INTEGER, "updateVersioned");
            RedisReply(this, reply->element[3], RedisReply::nonOwning)
                    .ensureType(REDIS_REPLY_INTEGER, "updateVersioned");
        }

        return static_cast<int>(incr->integer);
    }

    m_logger.warn("(updateVersioned) concurrency failure: too-many retries while reading " SPREDIS_SID_FMT,
                  SPREDIS_SID_FPARAM(id));
    return 0;
}

int spredis::RedisConnection::forceUpdate(const StorageId& id, const char* value, const time_t expiration) {
    m_logger.debug("(forceUpdate) updating key " SPREDIS_SID_FMT "@? (exp: %lld)",
                   SPREDIS_SID_FPARAM(id),
                   static_cast<long long>(expiration));
    RedisReply reply(this);
    const Lock ulock(m_mutex);

    // read tr. as pipeline
    appendCommand("MULTI");
    appendCommand("SET " SPREDIS_SID_FMT " %s XX KEEPTTL", SPREDIS_SID_FPARAM(id), value);
    appendCommand("INCR version.of:" SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(id));
    if (expiration != 0) {
        appendCommand("EXPIREAT " SPREDIS_SID_FMT " %lld",
                      SPREDIS_SID_FPARAM(id),
                      static_cast<long long>(expiration));
        appendCommand("EXPIREAT version.of:" SPREDIS_SID_FMT " %lld",
                      SPREDIS_SID_FPARAM(id),
                      static_cast<long long>(expiration));
    }
    appendCommand("EXEC");

    // exec transaction
    reply.getNextFromConnection("forceUpdate", "MULTI", REDIS_REPLY_STATUS);
    reply.getNextFromConnection("forceUpdate", "SET (data)", REDIS_REPLY_STATUS);
    reply.getNextFromConnection("forceUpdate", "INCR (version)", REDIS_REPLY_STATUS);
    if (expiration != 0) reply.getNextFromConnection("forceUpdate", "EXPIREAT (data)", REDIS_REPLY_STATUS);
    if (expiration != 0) reply.getNextFromConnection("forceUpdate", "EXPIREAT (version)", REDIS_REPLY_STATUS);
    reply.getNextFromConnection("forceUpdate", "EXEC", REDIS_REPLY_ARRAY);

    // incorrect amount of results is a fatal error
    // value + version + 2 * optional expiration
    if (reply->elements != 2 + 2 * (expiration != 0))
        handleCommandError("forceUpdate", "EXEC",
                           "incorrect amount of results from EXEC",
                           sizeof("incorrect amount of results from EXEC") - 1);

    RedisReply(this, reply->element[0], RedisReply::nonOwning)
            .ensureType(REDIS_REPLY_STATUS, "forceUpdate");

    const RedisReply incr(this, reply->element[1], RedisReply::nonOwning);
    incr.ensureType(REDIS_REPLY_INTEGER, "forceUpdate");

    if (expiration != 0) {
        RedisReply(this, reply->element[2], RedisReply::nonOwning)
                .ensureType(REDIS_REPLY_INTEGER, "forceUpdate");
        RedisReply(this, reply->element[3], RedisReply::nonOwning)
                .ensureType(REDIS_REPLY_INTEGER, "forceUpdate");
    }

    return static_cast<int>(incr->integer);
}

bool spredis::RedisConnection::remove(const StorageId& id) {
    m_logger.debug("(remove) deleting key " SPREDIS_SID_FMT "@?", SPREDIS_SID_FPARAM(id));
    const Lock ulock(m_mutex);

    RedisReply reply(this,
                     redisCommand(m_redis,
                                  "UNLINK " SPREDIS_SID_FMT " version.of:" SPREDIS_SID_FMT,
                                  SPREDIS_SID_FPARAM(id),
                                  SPREDIS_SID_FPARAM(id)));
    if (reply->type != REDIS_REPLY_INTEGER) return false;
    return reply->integer != 0;
}

void spredis::RedisConnection::appendCommand(const char* fmt, ...) const {
    va_list va;
    try {
        va_start(va, fmt);
        redisvAppendCommand(m_redis, fmt, va);
        va_end(va);
    } catch (...) {
        va_end(va);
        throw;
    }
}

void spredis::RedisConnection::recreateContext(int recurse) {
    const int result = redisReconnect(m_redis);
    if (result == REDIS_ERR) handleCriticalError("recreateContext", recurse);
}
