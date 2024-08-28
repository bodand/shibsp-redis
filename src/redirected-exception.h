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
 * redirected-exception.h
 *
 * Provides the RedirectedException exception type for the special error result
 * of Redis when running in cluster mode.
 */


#ifndef REDIRECTED_EXCEPTION_H
#define REDIRECTED_EXCEPTION_H

#include <string>
#include <exception>

#include "common.h"

namespace spredis {
    class RedirectedException SHIBSP_FINAL : std::exception {
        // note: exception type shouldn't be hidden, incase somehow leaves the
        //       SO file because then the RTTI wouldn't be available to properly
        //       catch it
    public:
        RedirectedException(const std::string& to_host, const unsigned int to_port)
            : to_host(to_host),
              to_port(to_port) {
        }

        const char* what() const SHIBSP_NOEXCEPT {
            // XXX this is not really informative, but this what() shouldn't be
            //     called in practive: RedisCluster always acts on it
            return "Redis cluster redirected operation to another node";
        }

        const std::string to_host;
        const unsigned int to_port;
    };
}

#endif //REDIRECTED_EXCEPTION_H
