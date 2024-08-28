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
 * cluster-node.h
 *
 * Provides the ClusterNode class representing a node in the cluster.
 */

#ifndef CLUSTER_NODE_H
#define CLUSTER_NODE_H

#include "common.h"
#include <string>

namespace spredis {
    class RedisConnection;
    class RedisConfig;

    /**
     * Class representing a node present in the cluster.
     * It stores the host and ip address of the node, and privdes a way to
     * connect to it.
     */
    class SHIBSP_HIDDEN ClusterNode SHIBSP_FINAL {
        std::string m_host;
        unsigned short m_port;

    public:
        ClusterNode(const std::string& host,
                    const unsigned short port)
            : m_host(host),
              m_port(port) {
        }

        bool valid() const { return !m_host.empty(); }

        const std::string& host() const {
            return m_host;
        }

        unsigned short port() const {
            return m_port;
        }

        RedisConnection* connect(const RedisConfig& config) const;

    private:
        friend bool operator==(const ClusterNode& lhs, const ClusterNode& rhs) {
            return lhs.m_host == rhs.m_host
                   && lhs.m_port == rhs.m_port;
        }

        friend bool operator!=(const ClusterNode& lhs, const ClusterNode& rhs) {
            return !(lhs == rhs);
        }
    };
}

#endif //CLUSTER_NODE_H
