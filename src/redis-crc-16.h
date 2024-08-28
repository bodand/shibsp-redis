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
 * redis-crc-16.h
 *
 * Provides the RedisCrc16 class which is responsible for performing the hash-slot
 * calculations when running in cluster mode.
 */

#ifndef REDIS_CRC_16_H
#define REDIS_CRC_16_H

#include <numeric>

#include "common.h"

namespace spredis {
    /**
     * Impementation of the XMODEM CRC 16 algorithm used by Redis
     * in hash slot distribution algorithm when taken mod 16384.
     * Note, that this result is not yet taken mod 16384, as such, this is the
     * true CRC value and not the redis hash-slot value.
     *
     * Provides the interface required by std::accumulate.
     */
    struct SHIBSP_HIDDEN RedisCrc16 SHIBSP_FINAL {
        static const unsigned int Initial;
        static const unsigned int HashSlotCount;

        unsigned int operator()(const unsigned int accumulator,
                                const char byte) const {
            return (accumulator << 8U & 0xFFFF)
                   ^ Crc16Constants[(accumulator >> 8U ^ static_cast<unsigned int>(byte)) & 0xFF];
        }

        /**
         * Shorthand for calculating the CRC16 hash of a range
         * given by begin and end character pointers.
         * Can be chained, by supplying an appropriate initial value.
         */
        static unsigned int calculate(const char* const begin,
                                      const char* const end,
                                      const unsigned int initial = Initial) {
            return std::accumulate(begin, end, initial, RedisCrc16{});
        }

    private:
        static const unsigned int Crc16Constants[32 * 8];
    };
}

#endif //REDIS_CRC_16_H
