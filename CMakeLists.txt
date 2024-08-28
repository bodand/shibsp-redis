cmake_minimum_required(VERSION 3.25)

project(shib-sp-redis
        DESCRIPTION "Redis support plugin for Shibboleth-SP v3"
        LANGUAGES CXX)

include(CMakeDependentOption)
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)

pkg_check_modules(shibsp REQUIRED IMPORTED_TARGET shibsp>=3.4.1)
pkg_check_modules(hiredis REQUIRED IMPORTED_TARGET hiredis>=0.14)

string(REGEX MATCH [=[[0-9]+\.]=] HIREDIS_MAJOR_VERSION "${hiredis_VERSION}")
message(STATUS "Found hiredis ${hiredis_VERSION}... using code for major-version ${HIREDIS_MAJOR_VERSION}")

if (NOT DEFINED SPREDIS_CAN_HAVE_TLS)
    if (hiredis_VERSION VERSION_GREATER 1.0.0)
        pkg_check_modules(hiredis_ssl IMPORTED_TARGET hiredis_ssl)
        set(SPREDIS_CAN_HAVE_TLS "${hiredis_ssl_FOUND}" CACHE INTERNAL "if required dependencies are present for TLS support" FORCE)
    else ()
        set(SPREDIS_CAN_HAVE_TLS NO CACHE INTERNAL "if required dependencies are present for TLS support" FORCE)
        message(WARNING "hiredis version ${hiredis_VERSION} does not provide built-in support for TLS-- disabling TLS code")
    endif ()
endif ()

cmake_dependent_option(SPREDIS_BUILD_TLS "Build TLS support for redis-store plugin [YES]" YES "SPREDIS_CAN_HAVE_TLS" OFF)

if (SPREDIS_CAN_HAVE_TLS AND SPREDIS_BUILD_TLS)
        set(SHIBSP_HAVE_HIREDIS_SSL YES)
else ()
        set(SHIBSP_HAVE_HIREDIS_SSL NO)
endif ()

configure_file(config.h.in config.h @ONLY)

add_library(redis-store MODULE
            src/cluster-range.h
            src/cluster-node.h
            src/redis.h
            src/cluster-range.cpp
            src/connection-lost-exception.h
            src/connection-lost-exception.cpp
            src/storage-id.h
            src/redirected-exception.h
            src/redirected-exception.cpp
            src/redis-crc-16.cpp
            src/redis-cluster.cpp
            src/redis-reply.h
            src/redis-reply.cpp
            src/redis-connection.${HIREDIS_MAJOR_VERSION}cpp
            src/common.h
            src/redis-connection.h
            src/redis-cluster.h
            src/redis-store.cpp
            src/redis.cpp
            src/cluster-node.cpp
            src/storage-id.cpp
            src/redis-connection.cpp
            src/redis-crc-16.h
            )

if (WIN32)
        target_sources(redis-store PRIVATE src/redis-store.rc)
endif()

set_target_properties(redis-store PROPERTIES 
                      PREFIX ""
                      VERSION "${shibsp_VERSION}"
                      SOVERSION "3")
target_link_libraries(redis-store PRIVATE PkgConfig::shibsp PUBLIC PkgConfig::hiredis)
target_include_directories(redis-store PRIVATE 
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

# Packaging
install(TARGETS redis-store 
        LIBRARY DESTINATION "${shibsp_LIBDIR}/shibboleth/"
        )
set(CPACK_PACKAGE_NAME "shibboleth-sp-redis")
set(CPACK_PACKAGE_FILE_NAME "shibboleth-sp-redis_${shibsp_VERSION}_amd64")
set(CPACK_PACKAGE_CONTACT "András Bodor <bodand@proton.me>")
set(CPACK_PACKAGE_VERSION "${shibsp_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION "Storage plugin for Shibboleth SP v3 providing support for using Redis as a storage backend.")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")

# debian
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "shibboleth-sp-utils (>= ${shibsp_VERSION}), libhiredis0.14")
set(CPACK_DEBIAN_PACKAGE_SECTION "web")

include(CPack)

