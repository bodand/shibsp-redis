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
 * connection-lost-exception.h
 *
 * Provides the ConnectionLostException exception type for when a node refuses
 * connections.
 */

#ifndef CONNECTION_LOST_EXCEPTION_H
#define CONNECTION_LOST_EXCEPTION_H

#include <xmltooling/exceptions.h>

#include "common.h"

namespace spredis {
    class ConnectionLostException SHIBSP_FINAL : public xmltooling::IOException {
    public:
        explicit ConnectionLostException(const std::string& msg, const xmltooling::params& p = xmltooling::params())
            : IOException(msg, p) {
        }

        ConnectionLostException(const char* const msg, const xmltooling::namedparams& p)
            : IOException(msg, p) {
        }

        ConnectionLostException(const std::string& msg, const xmltooling::namedparams& p)
            : IOException(msg, p) {
        }
    };
}

#endif //CONNECTION_LOST_EXCEPTION_H
