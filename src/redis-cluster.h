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
 * redis-cluster.h
 *
 * Provides the RedisCluster class that serves as a connection layer to the
 * multiple Redis instances in a given redis cluster.
 */

#ifndef REDIS_CLUSTER_H
#define REDIS_CLUSTER_H

#include <memory>

#include "cluster-range.h"
#include "common.h"
#include "connection-lost-exception.h"
#include "redirected-exception.h"
#include "redis.h"
#include "redis-connection.h"

#include <boost/container/stable_vector.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/map.hpp>

namespace spredis {
    class SHIBSP_HIDDEN RedisCluster SHIBSP_FINAL : public Redis {
        typedef RedisCrc16 hash_type;
        /**
         * The type representing a given range for hash-slots to belong to one
         * given node.
         */
        typedef ClusterRange<hash_type> range_type;

        /**
         * The stable-pointer flat-map to provide the mapping from
         * ClusterRange objects to their appropriate ClusterNode objects which
         * can connect to a single Redis cluster node.
         * The given sequence container (stable_vector) ensures that the objetcs
         * are stable in memory, and thusly their pointers can be safely taken.
         */
        typedef boost::container::flat_map<
            range_type,
            ClusterNode,
            ClusterCompareLess,
            boost::container::stable_vector<std::pair<range_type, ClusterNode> > > cluster_map_type;
        typedef cluster_map_type::iterator cluster_map_type_it;
        typedef cluster_map_type::const_iterator cluster_map_type_cit;
        typedef cluster_map_type::iterator::value_type cluster_map_iteration_type;

        /**
         * The flat-map to provide the mapping between the ClusterNode objects,
         * to cached connection objects.
         * The objects in this map, contrary to cluster_map, are not stable in
         * memory so DO NOT take their address in general.
         */
        typedef boost::container::map<
            const ClusterNode*,
            std::auto_ptr<RedisConnection> > connection_map_type;
        typedef connection_map_type::iterator connection_map_type_it;

    public:
        explicit RedisCluster(const RedisConfig& config);

        bool set(const StorageId& id, const char* value, time_t expiration);

        int getVersioned(const StorageId& id, std::string* out_value, time_t* out_expiration, int minVersion);

        int forceGet(const StorageId& id, std::string* out_value, time_t* out_expiration);

        int updateVersioned(const StorageId& id, const char* value, time_t expiration, int ifVersion);

        int forceUpdate(const StorageId& id, const char* value, time_t expiration);

        bool remove(const StorageId& id);

    protected:
        size_t scanContextTypeless(const char* context, RawCallbackType callback, void* callbackContext);

    private:
        template<class R, class CallFn>
        R wrappedCall(const StorageId& id, const CallFn& fn, int recurse = 0)
        try {
            const xmltooling::SharedLock slock(m_shared_mutex);
            const ClusterNode* const node = findNodeEntryUnguarded(id);
            RedisConnection* conn = dispatchConnectionUnguarded(node);
            return fn(conn);
        } catch (const ConnectionLostException&) {
            // retry connection some times recursively, and if all fails,
            // rethrow the connection error as we cannot handle it at our level
            // (this is needed because there may be some delay between the
            // syncronization of the failure to all masters in the cluster such
            // that they report the correct host:port for a range
            if (tryWaitWithRetryNumber(recurse)) {
                rebuildRangeMappingUniqueLocked();
                return wrappedCall<R>(id, fn, recurse + 1);
            }

            m_logger.error("Redis cluster failure: cannot find applicable host to connect to");
            throw;
        } catch (const RedirectedException& ex) {
            // retry connection some times recursively, and if all fails,
            // rethrow the connection error as we cannot handle it at our level
            // (this is needed because there may be some delay between the
            // syncronization of the failure to all masters in the cluster such
            // that they report the correct host:port for a range
            if (tryWaitWithRetryNumber(recurse)) {
                rebuildRangeMappingUniqueLocked();
                return wrappedCall<R>(id, fn, recurse + 1);
            }

            m_logger.error("Redis cluster failure: cannot connect to cluster after redirection: "
                           "redirected to `%s:%u' but could not reach node",
                           ex.to_host.c_str(), ex.to_port);
            throw;
        }

        void rebuildRangeMappingUniqueLocked();

        const ClusterNode* findNodeEntryUnguarded(const StorageId& id) const;

        const ClusterNode* findNodeEntryUnguarded(const ClusterNode& node) const;

        RedisConnection* dispatchConnectionUnguarded(const ClusterNode* node);

        void resetSlotsCacheUnguarded();

        bool tryWaitWithRetryNumber(int retry) const;

        struct CacheReseter {
            RedisCluster* cluster;

            explicit CacheReseter(RedisCluster* const cluster)
                : cluster(cluster) {
            }

            bool
            operator()(const cluster_map_iteration_type& entry) const;
        };

        struct CacheSetter {
            RedisCluster* cluster;

            explicit CacheSetter(RedisCluster* const cluster)
                : cluster(cluster) {
            }

            void
            operator()(const range_type& range, const ClusterNode& node) const;
        };

        friend CacheSetter;

        boost::scoped_ptr<xmltooling::RWLock> m_shared_mutex;
        connection_map_type m_connection_map;
        cluster_map_type m_cluster_map;
        RedisConfig m_config;
        xmltooling::logging::Category& m_logger;
        bool m_force_refresh_cluster_map;
    };
}

#endif //REDIS_CLUSTER_H
