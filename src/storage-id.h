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
 * storage-id.h
 *
 * Provides the StorageId class, which encapsulates a true identifier of a value
 * to be stored in the database.
 */


#ifndef STORAGE_ID_H
#define STORAGE_ID_H

#include "common.h"

#include <cstring>

/**
 * A format sub-string for printf-family functions to format a given storage
 * StorageId (SID) object.
 * Parameters should be passed via @c SPREDIS_SID_FPARAM .
 *
 * Example use:
 * @code
 * StorageId my_sid = ...;
 * printf("sid: " SPREDIS_SID_FMT, SPREDIS_SID_FPARAM(my_sid));
 * @endcode
 *
 * @see SPREDIS_SID_FPARAM
 */
#define SPREDIS_SID_FMT "{%s:%s%s}"

/**
 * Passes a StorageId object to a printf-family function the way to be handled
 * by @c SPREDIS_SID_FMT .
 *
 * @param sid The StorageId parameter to format
 *
 * @see SPREDIS_SID_FMT
 */
#define SPREDIS_SID_FPARAM(sid) sid.context(), sid.prefix(), sid.key()

namespace spredis {
    /**
     * A class encapsulating the multiple sections of making up the true key
     * of a value to be stored.
     * The class is immutable by design, because the key should not be changed
     * during processing, but purely passed through to Redis.
     */
    class SHIBSP_HIDDEN StorageId SHIBSP_FINAL {
        const char* m_context;
        const char* m_key;
        const char* const m_prefix;

    public:
        /**
         * Constructs a StorageId object with the provided sub-key parameters.
         * The object is immutable after this.
         *
         * @param context The context of the identifier.
         * @param key The inner key to be stored.
         * @param prefix Optional prefix value to prepend to the key.
         */
        StorageId(
            const char* context,
            const char* key,
            const char* const prefix = ""
        ) : m_context(context),
            m_key(key),
            m_prefix(prefix) {
        }

        /**
         * Returns the context of the composite identifier.
         *
         * @return The context of the identifier.
         */
        const char* context() const {
            return m_context;
        }

        /**
         * Returns the inner key of the composite identifier.
         * This key is the one set by the calling code to store the value.
         *
         * @return The inner key of the identifier.
         */
        const char* key() const {
            return m_key;
        }

        /**
         * Returns the optional prefix of the composite identifier.
         * If not present, the empty string is returned; no need to check for
         * null.
         *
         * @return The prefix of the identifier.
         */
        const char* prefix() const {
            return m_prefix;
        }

        template<class HashStrategy>
        unsigned hashSlotUsing() const {
            char colonBuf[] = ":";
            unsigned context = HashStrategy::calculate(m_context, m_context + std::strlen(m_context));
            unsigned colon = HashStrategy::calculate(colonBuf, colonBuf + 1, context);
            unsigned prefix = HashStrategy::calculate(m_prefix, m_prefix + std::strlen(m_prefix), colon);
            unsigned total = HashStrategy::calculate(m_key, m_key + std::strlen(m_key), prefix);
            return total % HashStrategy::HashSlotCount;
        }
    };
}

#endif //STORAGE_ID_H
