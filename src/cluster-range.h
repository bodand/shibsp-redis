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
 * cluster-range.h
 *
 * Provides the ClusterRange class for specifying a range of hash-slots.
 */

#ifndef CLUSTER_RANGE_H
#define CLUSTER_RANGE_H


#include <xmltooling/exceptions.h>

#include "common.h"
#include "storage-id.h"
#include "redis-crc-16.h"

namespace spredis {
    /**
     * A special std::less-compatible function object for ClusterRange objects.
     *
     * Required for C++98 compatibility, where std::less<T> cannot perform
     * heterogenous comparisions, only T against other T objects.
     */
    struct ClusterCompareLess;

    template<class HashStrategy = RedisCrc16,
        unsigned int HashSlots = 16384>
    class SHIBSP_HIDDEN ClusterRange SHIBSP_FINAL {
        const unsigned int m_start;
        const unsigned int m_end;

    public:
        ClusterRange(const unsigned int m_start, const unsigned int m_end)
            : m_start(m_start),
              m_end(m_end) {
            if (m_end < m_start)
                throw xmltooling::XMLToolingException("Hash-slot range ends before starting");
            if (HashSlots <= m_end)
                throw xmltooling::XMLToolingException("Hash-slot range ends after maximum slot count");
        }

        int from() const { return m_start; }
        int to() const { return m_end; }

        int compare(const ClusterRange& rhs) const {
            if (m_start < rhs.m_start) return -1;
            if (m_start > rhs.m_start) return 1;

            // starts equal
            if (m_end < rhs.m_end) return -1;
            if (m_end > rhs.m_end) return 1;

            return 0;
        }

        int compare(const StorageId& id) const {
            const unsigned int hashSlot = id.hashSlotUsing<HashStrategy>() % HashSlots;
            if (hashSlot < m_start) return 1;
            if (hashSlot > m_end) return -1;
            return 0;
        }

        bool operator<(const ClusterRange& rhs) const { return compare(rhs) < 0; }
        bool operator<=(const ClusterRange& rhs) const { return compare(rhs) < 0; }
        bool operator>(const ClusterRange& rhs) const { return compare(rhs) > 0; }
        bool operator>=(const ClusterRange& rhs) const { return compare(rhs) >= 0; }
        bool operator==(const ClusterRange& rhs) const { return compare(rhs) == 0; }
        bool operator!=(const ClusterRange& rhs) const { return compare(rhs) != 0; }

    private:
        friend bool operator<(const ClusterRange& lhs, const StorageId& rhs) { return lhs.compare(rhs) < 0; }
        friend bool operator<=(const ClusterRange& lhs, const StorageId& rhs) { return lhs.compare(rhs) <= 0; }
        friend bool operator>(const ClusterRange& lhs, const StorageId& rhs) { return lhs.compare(rhs) > 0; }
        friend bool operator>=(const ClusterRange& lhs, const StorageId& rhs) { return lhs.compare(rhs) >= 0; }
        friend bool operator==(const ClusterRange& lhs, const StorageId& rhs) { return lhs.compare(rhs) == 0; }
        friend bool operator!=(const ClusterRange& lhs, const StorageId& rhs) { return lhs.compare(rhs) != 0; }

        friend bool operator<(const StorageId& lhs, const ClusterRange& rhs) { return rhs.compare(lhs) > 0; }
        friend bool operator<=(const StorageId& lhs, const ClusterRange& rhs) { return rhs.compare(lhs) >= 0; }
        friend bool operator>(const StorageId& lhs, const ClusterRange& rhs) { return rhs.compare(lhs) < 0; }
        friend bool operator>=(const StorageId& lhs, const ClusterRange& rhs) { return rhs.compare(lhs) <= 0; }
        friend bool operator==(const StorageId& lhs, const ClusterRange& rhs) { return rhs.compare(lhs) == 0; }
        friend bool operator!=(const StorageId& lhs, const ClusterRange& rhs) { return rhs.compare(lhs) != 0; }
    };


    struct ClusterCompareLess {
        typedef int is_transparent;

        template<class Hash, unsigned int Slots>
        bool operator()(const ClusterRange<Hash, Slots>& lhs,
                        const ClusterRange<Hash, Slots>& rhs) const { return lhs < rhs; }

        template<class Hash, unsigned int Slots>
        bool operator()(const ClusterRange<Hash, Slots>& lhs,
                        const StorageId& rhs) const { return lhs < rhs; }

        template<class Hash, unsigned int Slots>
        bool operator()(const StorageId& lhs,
                        const ClusterRange<Hash, Slots>& rhs) const { return lhs < rhs; }
    };
}

#endif //CLUSTER_RANGE_H
