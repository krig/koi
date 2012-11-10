# A step by step guide to setting up a koi cluster

The purpose of this guide is to provide step-by-step instructions for
setting up and maintaining a high availability cluster using koi.

1. Compilation

2. Installation (Debian-based system)

3. Services

4. Active/passive services

5. Configuration

6. Monitoring the cluster

7. Maintenance mode

### Terminology

* `cluster` - A collection of nodes that together manage and monitor
  a set of highly available software.
* `node` - Single machine in a cluster.
* `elector` - Node that participates in the election process.
* `runner` - Node that executes and monitors services.

## Compilation

Right now, koi is hosted mainly at `https://github.com/krig/koi`.

After cloning the repository, the catch submodule needs to be fetched
for the unit tests to run correctly:

    git submodule init
    git submodule update

Koi uses waf to manage the build process. Waf is a python-based build
management tool similar to scons, with the benefit of being small
enough to include directly in the repository. Because of this, the
only requirements for compiling koi are:

 * GCC 4.4+ or Clang 3.1+
 * Python 2.5+
 * Boost 1.47+ (tested with boost <= 1.51)

Compilation and unit testing is done in two steps. First, waf needs to
be configured:

    ./waf configure

To use a different compiler (in this example, clang) simply set the
CXX and CC environment variables when configuring waf:

    CXX=clang++ CC=clang ./waf configure

Once waf has been configured and the dependencies are satisfied, the
koi executables can be built with this command:

    ./waf

This will produce `koi` and `koinode` executables in the `bin/`
subdirectory.

## Installation (Debian-based system)

As of this writing, the scripted installation is not working as it
should, so the koi executables need to be installed
manually. Fortunately, there isn't much to install.

`koi` and `koinode` should be installed to the `/usr/sbin` directory,
or equivalent.

By default, koi looks for its configuration file in
`/etc/koi/koi.conf` and for services to execute in the
`/etc/koi/services` directory.

The following commands will install the executables and create the
necessary folder structure on your filesystem.

    cp bin/koi bin/koinode /usr/sbin
    mkdir -p /etc/koi/services

This needs to be done on each machine that is to be included in the
koi cluster.

The next section will describe how to write the koi configuration
file, and the following section describes how to write a koi service.

## Configuration

A node can be configured as a `runner` or as an `elector`, or
both. This is done in the `node` section of the configuration file.

    node {
	  runner true
	  elector false
    }

By default, koi communicates on UDP port 42012. This can be changed by
configuring the `port` option in the `node` section in the
configuration file.

The most basic configuration features two nodes, preferrably with
multiple redundant interfaces. In this configuration, both nodes
should be configured as both runners and electors. In a cluster with
more than two nodes, it may make sense to configure some nodes to only
function as electors. It is also possible to configure the runner
nodes and elector nodes as independent entities, for example
configuring one elector machine that manages two runners.

There are many factors to consider in deciding on the topology of a
cluster, but a good rule of thumb is to ensure that there are more
than one runner, and that the number of electors is an odd number (3,
5, etc). For more details on configuring multiple electors, see the
section below on configuring `quorum`.

The next section in the configuration file tells the cluster how to
connect to other nodes. Koi can use multicast if available, but it can
also talk directly to a set of other nodes in the cluster through a
statically configured list of addresses. To use multicast, simply
configure a multicast address in the `transport` option. Koi will
automatically recognize that the address is a multicast address and
act accordingly.

The `password` option is used as an encryption key and should have the
same value across all nodes in the cluster. In the case of multiple
clusters communicating on the same multicast channel or set of UDP
ports, it is possible to configure a unique cluster ID for each
individual cluster. The `id` option should be a positive number. The
default cluster id is 13.

`transport` is a quoted list of comma-separated IP addresses, IP:port
pairs, multicast addresses or IPv6 addresses. Different types of
addresses can be freely mixed and matched.

For example, if configuring a cluster consisting of two machines,
where machine `a` has the IP `192.168.1.2` and machine `b` has the IP
`192.168.1.3` and the cluster shares the encryption password
`devpass`, the following configuration can be used on both machines:

    cluster {
      password devpass
      transport "192.168.1.2, 192.168.1.3"
    }

### Quorum

If you are configuring a cluster with more than two electors, it is
recommended that you also configure a `quorum` setting that is set to
2 or higher. This will prevent split brain scenarios, where a
partition in the network connectivity graph would cause multiple
runner nodes to be promoted to master at the same time. The
recommended `quorum` value is `f` where the number of elector nodes in
the cluster is `f + 1`. That is, with 3 electors in a cluster, the
`quorum` value should be set to 2.

## Services

A koi service consists of a named subdirectory in the
`/etc/koi/services` folder. The name should have the format
`[XX-]name` where `XX-` is an optional priority. These are all valid
service names:

    ping
	10-virtualip
	99-server

The service name cannot contain a period (`.`). Any service with a
name that contains one or more periods will be ignored by koi.

In principle, an empty directory is all that is required to have a
valid service. Koi will happily manage this null service even though
it doesn't have any service scripts, and will assume that all
scriptable events succeeded without incident.

To actually make the service do something useful, named scripts can be
put into the service folder. The possible scripts are:

    start
	stop
	promote
	demote
	status
	failed

As an example, the following service will run on all cluster nodes and
ping the machine at `example.com` at regular intervals. If
`example.com` doesn't reply, the node will fail and if promoted, will
be demoted and another node will be promoted instead.

To create this service, make a directory called
`/etc/koi/services/ping` and create a file inside this directory
called `status` with the following contents:

    #!/bin/sh
    ping -c 2 example.com
    exit $?

Make the `status` script executable: `chmod +x /etc/koi/services/ping/status`.


## Active/passive services

There are two ways that a service can act differently on the promoted
node compared to any other node in the cluster.

The first option is to look at the `KOI_IS_PROMOTED` environment
variable that is set by koi before calling a service script. This
variable is set to 1 if the node is promoted and otherwise 0.

The second option is to write service scripts to handle the `promote`
and `demote` events. For example, a virtual IP management script could
acquire the IP address in the `promote` script, and release the
virtual IP in the `demote` script. If this service should monitor the
state of the virtual IP in its `status` script, it can use the
`KOI_IS_PROMOTED` environment variable to do so.

## Starting koinode

To start koi on a node, use the `koinode` executable. This should
probably be started automatically by a system init script, but can be
launched manually for testing purposes. `koinode` will, by default,
write coloured log output to the console (stdout). This behaviour can
be changed via the `--log` command line option. To get koi to use
syslog, call it with `--log syslog` as an argument. To get koi to log
to both syslog and console, use `--log syslog,console`. To disable
terminal colors, pass `-C` to the command line.

By default, `koinode` will use the hostname of the machine as the node
identifier. This can be manually overridden via the `--name` command
line argument.

## Monitoring the cluster

An administrator can interact with the koi cluster via the `koi`
command line tool. `koi` will use the `koi.conf` configuration file to
find a cluster node to talk to if available, but can be configured to
talk to any IP and port via command line options. See `koi --help` for
more information on the available commands and options.

## Maintenance mode

To facilitate upgrades and reconfiguration of the cluster, a koi
cluster can be set to **maintenance mode**. Maintenance mode simply
means that koi will stop executing service scripts, and that all
events (start/stop/status etc) will be presumed to be successful. This
can be used when reconfiguring or modifying a service without
affecting other nodes in the cluster or risking spurious failover.

To set the cluster to maintenance mode:

    koi maintenance on

To take the cluster out of maintenance mode:

    koi maintenance off

