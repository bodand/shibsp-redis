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
 * common.h
 *
 * Header file containing definitions used by the storage plugin.
 */


#ifndef COMMON_H
#define COMMON_H

/* Define SHIBSP_FINAL to the final keyword, when compiling in C++11 and above */
/* Define SHIBSP_NOEXCEPT to the noexcept keyword, when compiling in C++11 and above
 * and to throw() othwerwise: required for std::exception::what() override */
#if __cplusplus > 201100
#define SHIBSP_FINAL final
#define SHIBSP_NOEXCEPT noexcept
#else
#define SHIBSP_FINAL
#define SHIBSP_NOEXCEPT throw()
#endif

/* Define SHIBSP_HIDDEN to GCC specific attributes to hide internal symbols when
 * compiling the plugin.
 * Only GCC is special cased, but Clang will trigger it as well. If building
 * on Windows, hiding is the default, so MSVC can be ignored.
 * Other compilers will miss out on a small optimization possibility, but we
 * can probably live with that.
 */
#if defined(__GNUC__) && __GNUC__ >= 4
#define SHIBSP_HIDDEN __attribute__((visibility("hidden")))
#else
#define SHIBSP_HIDDEN
#endif

#endif
