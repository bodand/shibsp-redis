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
 * redis.cpp
 *
 * Implementation of configuration parsing for Redis.
 */


#include "redis.h"

// XXX Win32 - special config headers
#include "config.h"

#include <algorithm>
#include <hiredis/hiredis.h>
#include <xmltooling/exceptions.h>

#ifdef SHIBSP_HAVE_HIREDIS_SSL
#include <hiredis/hiredis_ssl.h>
#endif

using namespace xercesc;
using namespace xmltooling;

namespace {
    const XMLCh host[] = UNICODE_LITERAL_4(h, o, s, t);
    const XMLCh port[] = UNICODE_LITERAL_4(p, o, r, t);
    const XMLCh prefix[] = UNICODE_LITERAL_6(p, r, e, f, i, x);
    const XMLCh connectTimeout[] = UNICODE_LITERAL_14(c, o, n, n, e, c, t, T, i, m, e, o, u, t);
    const XMLCh commandTimeout[] = UNICODE_LITERAL_14(c, o, m, m, a, n, d, T, i, m, e, o, u, t);
    const XMLCh nonBlocking[] = UNICODE_LITERAL_11(n, o, n, B, l, o, c, k, i, n, g);
    const XMLCh authUser[] = UNICODE_LITERAL_8(a, u, t, h, U, s, e, r);
    const XMLCh authPassword[] = UNICODE_LITERAL_12(a, u, t, h, P, a, s, s, w, o, r, d);
    const XMLCh retryAmount[] = UNICODE_LITERAL_11(r, e, t, r, y, A, m, o, u, n, t);
    const XMLCh retryBaseTime[] = UNICODE_LITERAL_13(r, e, t, r, y, B, a, s, e, t, i, m, e);
    const XMLCh retryMaxTime[] = UNICODE_LITERAL_12(r, e, t, r, y, M, a, x, t, i, m, e);

    const XMLCh Cluster[] = UNICODE_LITERAL_7(C, l, u, s, t, e, r);

    const XMLCh Host[] = UNICODE_LITERAL_4(H, o, s, t);

    const XMLCh Tls[] = UNICODE_LITERAL_3(T, l, s);
    const XMLCh clientCert[] = UNICODE_LITERAL_10(c, l, i, e, n, t, C, e, r, t);
    const XMLCh clientKey[] = UNICODE_LITERAL_9(c, l, i, e, n, t, K, e, y);
    const XMLCh caBundle[] = UNICODE_LITERAL_8(c, a, B, u, n, d, l, e);
    const XMLCh caDirectory[] = UNICODE_LITERAL_11(c, a, D, i, r, e, c, t, o, r, y);

    std::vector<spredis::ClusterNode> readClusterConfig(const DOMElement* const e,
                                                        const unsigned short defaultPort) {
        std::vector<spredis::ClusterNode> nodes;

        // check if cluster config, otherwise return empty array
        const DOMElement* cluster = XMLHelper::getFirstChildElement(e, Cluster);
        if (cluster == NULL) return nodes;

        // check Cluster children; ensure at least one is present
        const DOMNodeList* const hosts = cluster->getChildNodes();
        if (hosts->getLength() == 0)
            throw XMLToolingException("At least one Host node must be specified in Cluster configuration");

        for (XMLSize_t i = 0; i < hosts->getLength(); ++i) {
            const DOMNode* host = hosts->item(i);

            // ignore comments
            if (host->getNodeType() == DOMNode::COMMENT_NODE) continue;

            // ignore whitespace
            if (host->getNodeType() == DOMNode::TEXT_NODE) {
                char* buf = XMLString::transcode(host->getNodeValue());
                std::string textData(buf);
                XMLString::release(&buf);

                const bool isWhitespace = std::all_of(textData.cbegin(), textData.cend(), ::iswspace);
                if (isWhitespace) continue;
            }

            // ensure only Host nodes are present as children of Cluster
            if (host->getNodeType() != DOMNode::ELEMENT_NODE
                || XMLString::compareString(host->getLocalName(), Host) != 0) {
                char* buf = XMLString::transcode(host->getNodeName());
                std::string hostName(buf);
                XMLString::release(&buf);

                throw XMLToolingException(
                    "Only Host nodes may be present as children of Cluster: found `" + hostName + "'");
            }

            const DOMElement* const element = dynamic_cast<const DOMElement*>(host);
            assert(element != NULL); // type checked above

            auto_ptr_char hostString(XMLHelper::getTextContent(element));
            spredis::ClusterNode node(
                hostString.get(),
                static_cast<unsigned short>(
                    XMLHelper::getAttrInt(element, defaultPort, ::port)
                ));
            nodes.push_back(node);
        }

        return nodes;
    }

    std::string attributeIfElementExists(const DOMElement* e,
                                         const char* def, const XMLCh* name) {
        if (!e) return def;
        return XMLHelper::getAttrString(e, def, name);
    }
}

spredis::RedisTlsConfig::RedisTlsConfig(const DOMElement* e)
    : enabled(e != NULL),
      clientCert(attributeIfElementExists(e, "", ::clientCert)),
      clientKey(attributeIfElementExists(e, "", ::clientKey)),
      caBundle(attributeIfElementExists(e, "", ::caBundle)),
      caDirectory(attributeIfElementExists(e, "", ::caDirectory)) {
    // if not enabled, nothing is to be done
    if (!enabled) return;

#ifndef SHIBSP_HAVE_HIREDIS_SSL
    throw XMLToolingException("TLS is configured but TLS support was not compiled in this daemon."
                              "If you built Shiboleth yourself, ensure that hiredis was built with"
                              "OpenSSL support enabled, and reconfigure Shibboleth.");
#else
    if (!e->hasAttribute(::clientCert)
        || !e->hasAttribute(::clientKey))
        throw XMLToolingException("TLS is enabled, but client certificate or key are not set."
            "If you don't want to use mTLS, explicitly set them to the empty string.");

    redisInitOpenSSL();
#endif
}

spredis::RedisConfig::RedisConfig(const DOMElement* const e)
    : host(XMLHelper::getAttrString(e, "localhost", ::host)),
      port(static_cast<unsigned short>(
          XMLHelper::getAttrInt(e, 6379, ::port)
      )),
      prefix(XMLHelper::getAttrString(e, "", ::prefix)),
      initialNodes(readClusterConfig(e, port)),
      connectTimeoutMillisec(XMLHelper::getAttrInt(e, 0, connectTimeout)),
      commandTimeoutMillisec(XMLHelper::getAttrInt(e, 0, commandTimeout)),
      nonBlocking(XMLHelper::getAttrBool(e, false, ::nonBlocking)),
      authnUsername(XMLHelper::getAttrString(e, "", authUser)),
      authnPassword(XMLHelper::getAttrString(e, "", authPassword)),
      maxRetries(static_cast<unsigned int>(
          XMLHelper::getAttrInt(e, 5, retryAmount)
      )),
      baseWait(static_cast<unsigned int>(
          XMLHelper::getAttrInt(e, 500, retryBaseTime)
      )),
      maxWait(static_cast<unsigned int>(
          XMLHelper::getAttrInt(e, 0, retryMaxTime)
      )),
      tls(XMLHelper::getFirstChildElement(e, Tls)) {
}
