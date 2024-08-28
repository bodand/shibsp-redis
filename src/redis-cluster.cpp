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
 * redis-cluster.cpp
 *
 * Implements the Redis clustering logic's client side for the RedisCluster
 * type.
 */

#include <algorithm>

#include "redis-cluster.h"
#include "redirected-exception.h"

#include <boost/lambda/core.hpp>
#include <boost/lambda/detail/bind_functions.hpp>
#include <boost/lambda/detail/lambda_functor_base.hpp>

using namespace xmltooling;
using namespace boost;

namespace {
    /**
     * A lock for getting write ownership of a shared mutex (RWLock). Not usable
     * in a general sense, as only supports our required use-case.
     *
     * The built-in locks only support SharedLock and the default Lock does
     * not support uniquely locking a shared mutex...
     */
    class UniqueLock {
        MAKE_NONCOPYABLE(UniqueLock);

    public:
        explicit UniqueLock(const scoped_ptr<RWLock>& shared) : shared_mutex(shared.get()) {
            if (shared_mutex) shared_mutex->wrlock();
        }

        ~UniqueLock() {
            if (shared_mutex) shared_mutex->unlock();
        }

    private:
        RWLock* shared_mutex;
    };
}

spredis::RedisCluster::RedisCluster(const RedisConfig& config)
    : Redis(config.prefix),
      m_shared_mutex(RWLock::create()),
      m_connection_map(),
      m_cluster_map(),
      m_config(config),
      m_logger(log4shib::Category::getInstance("XMLTooling.StorageService.REDIS")),
      m_force_refresh_cluster_map(false) {
    UniqueLock ulock(m_shared_mutex);

    for (size_t i = 0; i < config.initialNodes.size(); ++i) {
        const ClusterNode& node = config.initialNodes[i];
        try {
            // throwaway connection: this cannot be cached, because it is not in
            // the correct node storage (m_cluster_map) so it will never be hit
            // anyways, just wasting the cache space
            const scoped_ptr<RedisConnection> conn(node.connect(config));
            conn->iterateSlots(CacheSetter(this));
            break;
        } catch (const std::exception& ex) {
            m_logger.error("error occured during initial cluster configuration from %s:%u -- skipping node: %s",
                           node.host().c_str(), node.port(), ex.what());
        } catch (...) {
            m_logger.error("unknown error occured during initial cluster configuration from %s:%u -- skipping node",
                           node.host().c_str(), node.port());
        }
    }
}

void spredis::RedisCluster::rebuildRangeMappingUniqueLocked() {
    UniqueLock ulock(m_shared_mutex);
    resetSlotsCacheUnguarded();
}

bool spredis::RedisCluster::set(const StorageId& id, const char* value, time_t expiration) {
    // XXX boost::lambda should be replaced with C++11 lambda when possible
    using namespace boost::lambda;
    return wrappedCall<bool>(id, bind(&RedisConnection::set, _1, id, value, expiration));
}

int spredis::RedisCluster::getVersioned(const StorageId& id, std::string* out_value, time_t* out_expiration,
                                        const int minVersion) {
    // XXX boost::lambda should be replaced with C++11 lambda when possible
    using namespace boost::lambda;
    return wrappedCall<int>(id, bind(&RedisConnection::getVersioned, _1, id, out_value, out_expiration, minVersion));
}

int spredis::RedisCluster::forceGet(const StorageId& id, std::string* out_value, time_t* out_expiration) {
    // XXX boost::lambda should be replaced with C++11 lambda when possible
    using namespace boost::lambda;
    return wrappedCall<int>(id, bind(&RedisConnection::forceGet, _1, id, out_value, out_expiration));
}

int spredis::RedisCluster::updateVersioned(const StorageId& id, const char* value, time_t expiration, int ifVersion) {
    // XXX boost::lambda should be replaced with C++11 lambda when possible
    using namespace boost::lambda;
    return wrappedCall<int>(id, bind(&RedisConnection::updateVersioned, _1, id, value, expiration, ifVersion));
}

int spredis::RedisCluster::forceUpdate(const StorageId& id, const char* value, time_t expiration) {
    // XXX boost::lambda should be replaced with C++11 lambda when possible
    using namespace boost::lambda;
    return wrappedCall<int>(id, bind(&RedisConnection::forceUpdate, _1, id, value, expiration));
}

bool spredis::RedisCluster::remove(const StorageId& id) {
    // XXX boost::lambda should be replaced with C++11 lambda when possible
    using namespace boost::lambda;
    return wrappedCall<bool>(id, bind(&RedisConnection::remove, _1, id));
}

size_t spredis::RedisCluster::scanContextTypeless(const char* context,
                                                  RawCallbackType callback,
                                                  void* callbackContext) {
    const SharedLock slock(m_shared_mutex);

    // XXX lambda when possible
    struct callback_wrap {
        callback_wrap(const RawCallbackType callback, void* const callback_context)
            : callback(callback),
              callbackContext(callback_context) {
        }

        void
        operator()(RedisConnection* connection, const std::string& data) const {
            callback(callbackContext, connection, data);
        }

        RawCallbackType callback;
        void* callbackContext;
    };

    for (cluster_map_type_cit it = m_cluster_map.cbegin();
         it != m_cluster_map.cend();
         ++it) {
        RedisConnection* conn = dispatchConnectionUnguarded(&it->second);
        // this call is tricky, we wrap our typeless callback into a typed
        // callback, which will perform the same transformation we did, when
        // we got the outermost callback, so when calling this callback, two
        // layers of this type-erasure trickery will happen:
        //   1) in the impl of conn->scanContext
        //   2) in our callback call in callback_wrap,
        // in this order
        conn->scanContext(context, callback_wrap(callback, callbackContext));
    }

    return 0U;
}

const spredis::ClusterNode* spredis::RedisCluster::findNodeEntryUnguarded(const StorageId& id) const {
    const cluster_map_type_cit it = m_cluster_map.find(id);
    if (it == m_cluster_map.cend()) return NULL;
    return &it->second;
}

const spredis::ClusterNode* spredis::RedisCluster::findNodeEntryUnguarded(const ClusterNode& node) const {
    // This is a more expensive operation compared to finding by StorageId,
    // because it is essentially a linear search in the values stored in a map.
    // Nevertheless, this should matter much as this is only used when
    // recovering from a RedirectedException as a way to fast-track the actual
    // operation, before truly dealing with the problem
    struct finder {
        explicit finder(const ClusterNode& node)
            : node(node) {
        }

        const ClusterNode& node;

        bool
        operator()(const cluster_map_type::value_type& kv) const {
            return kv.second == node;
        }
    };

    const cluster_map_type_cit it = std::find_if(m_cluster_map.cbegin(), m_cluster_map.cend(), finder(node));
    if (it == m_cluster_map.cend()) return NULL;
    return &it->second;
}

spredis::RedisConnection* spredis::RedisCluster::dispatchConnectionUnguarded(const ClusterNode* node) {
    connection_map_type_it it = m_connection_map.find(node);
    if (it == m_connection_map.end()) {
        const std::pair<connection_map_type_it, bool> insert_result =
                m_connection_map.insert(std::make_pair(node,
                                                       node->connect(m_config)));
        it = insert_result.first;
    }

    return it->second.get();
}

namespace {
    const bool continueIteration = false;
    const bool stopIteration = true;
}

void spredis::RedisCluster::resetSlotsCacheUnguarded() {
    // flush current conneciton cache: flushing cache first ensures we _somehow_
    // don't accidentally dereference the cached pointers that are danling after
    // clearing m_cluster_map
    m_connection_map.clear();

    // clear current range-node mapping
    // XXX Some clever algorithm could be devised to allow the reuse of already
    //     existing range-node entries that do not change, but because cache is
    //     implemented on top of the current node object's memory address, this
    //     algorithm needs to be really clever around how to insert/remove
    //     range-node mappings and when to invalidate cached connections. A
    //     major potential problem seems to be ranges that overlap with previous
    //     range entries as that requires complete manual special-casing.
    //     -- this is not deemed worth the complex code right now, the below is
    //        quite simple, if not the most efficient because of the cache reuse
    //        inefficiencies, nevertheless, this code only runs when the cluster
    //        config changes (either via manual redistribution of slots, or auto
    //        failover) and once on startup, neither occurs *that* frequently
    cluster_map_type previous_map = m_cluster_map;
    m_cluster_map.clear();

    // take the first successful iteration of the result of CLUSTER SLOTS
    // and set m_cluster_map appropriately
    const bool success = std::any_of(previous_map.begin(),
                                     previous_map.end(),
                                     CacheReseter(this));
    if (!success) {
        m_logger.crit("no known node configured in the redis cluster responds correctly to `CLUSTER "
            "SLOTS': cannot explore cluster topology");
        throw XMLToolingException("Cannot connect to any nodes in the redis cluster");
    }

    m_force_refresh_cluster_map = false;
}

namespace {
    unsigned waitTime(const unsigned config) {
        if (config == 0) return static_cast<unsigned>(-1);
        return config;
    }
}

bool spredis::RedisCluster::tryWaitWithRetryNumber(const int retry) const {
    const unsigned int retryUnsigned = static_cast<unsigned int>(retry);
    if (retryUnsigned > m_config.maxRetries) return false;

    const unsigned int toWait = m_config.baseWait * (1 << retryUnsigned);
    const unsigned int usTrueWait = 1000 * std::min(toWait, waitTime(m_config.maxWait));

    m_logger.debug("waiting about %u milliseconds for try %u/%u",
                   usTrueWait / 1000, retryUnsigned, m_config.maxRetries);
    usleep(usTrueWait); // XXX Win32 - usleep
    return true;
}

bool spredis::RedisCluster::CacheReseter::operator()(const cluster_map_iteration_type& entry) const
try {
    cluster->m_logger.debug("trying reading configuration from node %s:%u (currently known for range %d-%d)",
                            entry.second.host().c_str(), entry.second.port(),
                            entry.first.from(), entry.first.to());
    RedisConnection* conn = cluster->dispatchConnectionUnguarded(&entry.second);
    conn->iterateSlots(CacheSetter(cluster));
    return stopIteration;
} catch (const std::exception& ex) {
    cluster->m_logger.error("error occured getting cluster configuration from %s:%u -- skipping node: %s",
                            entry.second.host().c_str(), entry.second.port(),
                            ex.what());
    return continueIteration;
} catch (...) {
    cluster->m_logger.error("unknown error occured getting cluster configuration from %s:%u -- skipping node",
                            entry.second.host().c_str(), entry.second.port());
    return continueIteration;
}

void spredis::RedisCluster::CacheSetter::operator()(const range_type& range,
                                                    const ClusterNode& node) const {
    cluster->m_logger.debug("Redis cluster hash-range: %d-%d to host %s:%u",
                            range.from(), range.to(), node.host().c_str(), node.port());
    cluster->m_cluster_map.insert_or_assign(range, node);
}
