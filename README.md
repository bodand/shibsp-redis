# redis-store

This storage plugin provides support for storing session information in the [Redis][redis] key-value database.
The plugin can be configured to use either a single-instance Redis database, or a Redis cluster: see [Configuration](#configuration).

## Packaging (Debian)

For building a locally usable Debian package from the repository, perform the following.

Prerequisites: 
- A Debian system with the `build-essential cmake libshibsp-dev libhiredis-dev` packages installed.

Building, from the repository directory:

```shell
$ cmake -S. -B_build
$ cmake --build _build # --parallel <N> if desired
$ cd _build 
$ cpack -G DEB
```

After this there will be a `shibboleth-sp-redis_<version>_<arch>.deb` file in the `_build` directory, which depends (in theory) on the current `libshibsp-dev` version; this is enforced by depending (in the package) on the `shibboleth-sp-utils` package with the same version.
This package contains the runtime needed to load the plugin anyways, so the version enforcement just helps matching the versions.
Whenever a new Shibbolteth SP is released a rebuild of the plugin is needed with the maching `libshibsp-dev` package.

## Building (full)

The plugin uses the [hiredis][hiredis] C library for low-level connection to a Redis database instance, thus this library needs to be available when configuring Shibboleth-SP.
Any version in 1.x.x should work fully, versions before the stable 1.0.0 release will build, but some configuration parameters will be ignored.

If TLS support is required, hiredis itself needs to be built with TLS support.
This requires hiredis 1.0.0 or above.
See their documentation for up-to-date info about how to do that.
The plugin builds and works without TLS enabled, but a configuration that needs TLS will not work with a storage plugin built without TLS support.

The hiredis distribution contains a pkg-config file, which should be installed when building the library.
If the library was installed in a path known by pkg-config, nothing else needs to be done.
Otherwise, the `PKG_CONFIG_PATH` needs to be set to contain the appropriate location for the configure script.

Other than that, the Boost libraries are required: this subproject specifically only requires Boost.Container and Boost.Lambda, but usually the easiest option is to just get all header-only Boost libraries, as they have rather intricate interdependencies which make it easier to just ignore and get the whole package.

When the above preparations are done, configuration of the SP can proceed as usual: to enable Redis support, provide the `--with-redis` flag.
This will instruct the script to search for hiredis and activate this subproject.
Nothing is changed in the steps afterward.

For example, if installed hiredis to `/opt/shibboleth-sp` with all other Shibboleth packages, configuration may look like:

```shell
$ PKG_CONFIG_PATH=/opt/shibbolet-sp/lib/pkgconfig/ ./configure --prefix=/opt/shibbolet-sp/ --with-redis <other-options>
```

### Windows

Windows builds are not currently supported.
This is mostly because of the build-system; the code itself is mostly cross-platform.
The code is annotated with `XXX Win32` where it is believed special handling is required to make a Windows build successful.

## Configuration

After building and installing the plugin with the SP it needs to be configured.
The `StorageService` node is used for this, in the configuration XML file, where
the type is set to `REDIS`.

There are two ways to configure the storage plugin: single-instance and cluster.
In single instance mode there is only a single Redis instance running and everything is to be done there.
In cluster mode there are multiple Redis instances and the storage plugin needs to properly handle distribution of writes and reads to the appropriate node.

The following tables list the available configuration properties, examples are present at the end of the section.

### Common configuration

These configuration fields can be set on both operational modes.

#### Attributes

A lot of low-level parameters about the connection can be configured.
Most of the settings in this group are related to these low-level settings, and thus can be safely ignored in most cases.

**WARNING** The prefix value **must not** contain curly braces, the behavior of the storage plugin is undefined otherwise.

| Name              | Type     | Default | Description                                                                                                                                   |
|-------------------|----------|---------|-----------------------------------------------------------------------------------------------------------------------------------------------|
| type (required)   | string   | N/A     | Specifies the type of StorageService plugin, set to "REDIS" for this plugin.                                                                  |
| id                | XML ID   |         | A unique identifier within the configuration file that labels the plugin instance so other plugins can reference it.                          |
| prefix            | string   | ""      | String prefix that gets prepended to the keys so that there are no key name conflicts when different applications use the same Redis servers. |
| nonBlocking       | bool     | false   | Connect using a non-blocking socket.                                                                                                          |
| connectionTimeout | int (ms) | 0       | Wait this amount in milliseconds for a connection to be established before giving up. 0 means 'use library default'.                          |
| commandTimeout    | int (ms) | 0       | Wait this amount in milliseconds for a command to complete before giving up. 0 means 'use library default'.                                   |
| retryAmount       | int      | 5       | How many times to retry in case a non-fatal error occurs (cluster configuration changed, or a connection was lost). See below.                |
| retryBaseTime     | int (ms) | 500     | The base time used to calculate waiting before retrying the failed operation. See below.                                                      |
| retryMaxTime      | int (ms) | 0       | The maximum time that can be waited before retrying. 0 means no maximum. See below.                                                           |
| authUser          | string   | ""      | Sets authentication user. See _AUTH parameters_ below.                                                                                        |
| authPassword      | string   | ""      | Sets authentication password. See _AUTH parameters_ below.                                                                                    |

*hiredos 0.14 limitations*

When building with hiredis versions before 1.0.0, the `connectionTimeout` options is ignored, and `commandTimeout` is set as the only configurable timeout setting in hiredis 0.14.1.

*Retries and timing*

In case a Redis instance is not available, because of a lost connection, or, in cluster mode, a node-failure causes cluster reconfiguration, the operation is retried automatically.
This behavior can be disabled by setting `retryAmount` to 0.
Otherwise, a command is retried the amount of times set in that configuration value.

The failed operation is not retried immediately to allow Redis to actually perform the cluster's failover behavior, without just generating more errors on our side.
Initially, the amount waited before a retried operation is the value set in `retryBaseTime`.
Any subsequent retries are calculated by multiplying this value by the _n - 1_-th power of 2, where _n_ is the number of the retry, that is using exponential backoff.
By default, there is no limitation on the amount of time that can be waited in one retry attempt, but it can be configured using the `retryMaxTime` attribute.
If this is set, the amount waited is capped to this value for one given attempt.
Setting this to retryBaseTime, in practice, disables the exponential behavior and creates a flat wait time.

*AUTH parameters*

After connecting to a Redis server, the client supports sending authentication information using the `AUTH` command.
Since Redis 6.0.0 this can be a username and a password, before that, only a password was used.
The parameters to be used, if any, can be configured using the `authUser` and `authPassword` attributes.
If neither are (default) or only `authUser` is set, authentication is not performed when connecting to a server.
If only a password is provided, the old user-less authentication is performed.
If both are set the new authentication scheme is used, that allows ACLs to be set to different users.

### Single-instance configuration

These settings are relevant when configuring the storage plugin in single-instance mode.

#### Attributes

| Name | Type   | Default   | Description                                               |
|------|--------|-----------|-----------------------------------------------------------|
| host | string | localhost | The host address of the Redis server to connect to.       |
| port | int    | 6379      | The port where the Redis server to connect to is running. |

### Cluster configuration

These settings are relevant when configuring the storage plugin in cluster mode.

#### Attributes

| Name | Type | Default | Description                               |
|------|------|---------|-------------------------------------------|
| port | int  | 6379    | The default port to use on cluster hosts. |

#### Child Elements (StorageService)

| Name    | Cardinality | Description                                                     |
|---------|-------------|-----------------------------------------------------------------|
| Tls     | 0-1         | If present, denotes that the configuration uses TLS.            |
| Cluster | 0-1         | If present, denotes that the configuration is for cluster mode. |

#### Attributes (Tls)

| Name                  | Type   | Default | Description                                                                |
|-----------------------|--------|---------|----------------------------------------------------------------------------|
| clientCert (required) | string | N/A     | The client's mTLS certificate.                                             |
| clientKey (required)  | string | N/A     | The client's mTLS secret key.                                              |
| caBundle              | string | ""      | Set the list of trusted CA certificates using the specified CA bundle.     |
| caDirectory           | string | ""      | Set the list of trusted CA certificates using a directory containing them. |

*mTLS certificate*

Since Redis server by default requires mTLS when TLS is enabled, so does this configuration.
If you do not wish to use mTLS, explicitly set `clientCert` and `clientKey` to the empty string.

#### Child Elements (Cluster)

| Name | Cardinality | Description             |
|------|-------------|-------------------------|
| Host | 1-*         | The address of the host |

#### Attributes (Host)

| Name | Type | Default             | Description                                                                                                                        |
|------|------|---------------------|------------------------------------------------------------------------------------------------------------------------------------|
| port | int  | StorageService@port | The port where the Redis server to connect to is running on this host. Overrides the setting at the enclosing StorageService node. |

### Examples

A single instance configuration:

```xml
<!-- Example 1. Single instance configuration. 
     The port set to 6379 is superfluous, since that is the default, but is set 
     here for demonstration purposes.
  -->
<!-- Configure the storage service -->
<StorageService type="REDIS"
                id="my_redis_ss"
                host="10.1.2.3"
                port="6379"
                prefix="my_ssp:"/>
        <!-- Use the storage service, e.g. as SessionCache, irrelevant options omitted -->
<SessionCache type="StorageService"
              StorageService="my_redis_ss"/>
```

Redis cluster configuration:
Technically only one (master or replica) node is enough to configure, because the plugin loads the actual topology after connecting, it is advised to set as many fields as possible.
If only one node is provided and that happens to fail when the plugin is started, the connection, and with it the whole plugin will fail; which in turn will leave the shibd process without a proper storage plugin.

Note that if a `Cluster` child element is present, the single-instance configuration value `host` is silently ignored, while `port` is used to specify the default port on `Host` nodes when it is not provided.
That is, if the `StorageService` node has the `port` attribute set, each `Host` will behave as if that value is set on its own `port` attribute, unless explicitly set otherwise.
See Example 3 below.

```xml
<!-- Example 2. A clustered redis configuration with at least 3 nodes in the cluster.
     The given host:port combinations can refer to both masters and replicas.
  -->
<StorageService type="REDIS" id="my_redis_ss" prefix="my_ssp:">
    <Cluster>
        <Host>10.1.2.1</Host>             <!-- port = 6379 -->
        <Host port="6380">10.1.2.2</Host> <!-- port = 6380 -->
        <Host>10.1.2.3</Host>             <!-- port = 6379 -->
    </Cluster>
</StorageService>
        <!-- usage as before in single instance configuration -->
```

```xml
<!-- Example 3. Same topology as in Example 2., but the default port on unspecified
     Host nodes is now set to 9876 in the StorageService node.
  -->
<StorageService type="REDIS" id="my_redis_ss" port="9876" prefix="my_ssp:">
    <Cluster>                            <!-- ~~~~~~^^^^~ -->
        <Host>10.1.2.1</Host>             <!-- port = 9876 -->
        <Host port="6380">10.1.2.2</Host> <!-- port = 6380 -->
        <Host>10.1.2.3</Host>             <!-- port = 9876 -->
    </Cluster>
</StorageService>
        <!-- usage as before in single instance configuration -->
```

```xml
<!-- Example 4. A simple TLS configuration with mTLS support enabled on the 
     client side. The TLS configuration is the same for single-instance and
     cluster configurations: this example uses the previously seen cluster.
     (This requires hiredis version 1.0.0 or greater (Debian, as of now, 
     packages 0.14.x)).
  -->
<StorageService type="REDIS" id="my_redis_ss" prefix="my_ssp:">
    <Cluster>
        <Host>10.1.2.1</Host>
        <Host port="6380">10.1.2.2</Host>
        <Host>10.1.2.3</Host>
    </Cluster>
    <Tls clientCert="my.crt.pem"
         clientKey="my.key.pem"/>
</StorageService>
        <!-- usage as before -->
```

## Caveats and limitations

1. During authentication support is not checked for two param `AUTH`, so if configured to behave so, while using Redis older than 6.0.0 all connections will fail.
2. The password set as to log in with is stored in memory in plain text. If an attacker has gained sufficient access to query process memory, they can read it. This is not believed to be a major problem.
3. Because of hiredis representing ports as `int`, on some architectures where `sizeof(int) == 2`, port numbers above 32768 will not behave correctly. (Assuming a byte is 8 bits on the architecture in question.)
4. The TLS code does not support setting SNI values.

[redis]: https://redis.io

[hiredis]: https://github.com/redis/hiredis
