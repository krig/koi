# KOI

Cluster management/high availability software for small-scale
redundancy.

For license information, see LICENSE.

## Introduction

Koi is an internal tool I threw together at Procera after years of
frustration trying to get Pacemaker to do what we wanted. The main
purpose of koi is to maintain a small cluster of machines (usually 3
or 5) where a subset of those machines run _services_. Services
usually consist of a set of bash scripts that start, stop and monitor
some other piece of software. This software only runs in active mode
on one of the machines, with the other nodes functioning as backup
servers. In the event of a monitoring error, the software is stopped
on the faulty node and started on another machine.

This is the first public release of koi, and it is a little rough
around the edges. There isn't much documentation or much of a guide
for setting it up, and the user interface needs work. It is, however,
basically the same version that we are using in production.

In koi, a cluster consists of elector nodes and runner nodes. A node
can be both an elector and a runner simultaneously, and the most
common setup we've deployed is a cluster with 2 runners and 1 or 2
additional electors to help avoid split brain scenarios.

One elector node will be automatically picked as the leader node. This
node will then promote one of the runner nodes as the master node. So:
Elector nodes have one automatically selected leader, and that leader
selects one runner node to be the master runner. The reason for
separating the automatically selected leader from the master role is
because we usually want to be able to manually select which node is
the master.

Runner nodes are the nodes that actually run services. A service is a
set of scripts similar to those used by [runit][runit], that are
executed to control a particular service. The possible scripts are
start, stop, promote, demote, failed and status.

Start and stop are executed when the node is started and
stopped. Promote and demote are executed on the master runner as it is
promoted or demoted to and from the master role. Status is executed
periodically to monitor the health of the service, and failed is
called if the node experiences failure (for example if a service fails
on the node).

Services can depend on each other via simple priorities: A service at
priority 5 will always be started and promoted before a service at
priority 10, and will be stopped and demoted after the service with a
higher priority. Services that don't have a priority assigned to them
are started and stopped independently of other services.

## What can koi be used for?

The main use case is to provide redundancy for some piece of software
that can run in both active and passive mode, and where one instance
of the program should be active at any given time.

## Election algorithm

Since the focus of koi is small, usually site-local clusters, a simple
election algorithm seems to work well. The basic algorithm is the
Bully Algorithm as described [here][bully].

## Compilation

Koi is written in C++ and compiles for Mac OS X and Linux. It expects
to be deployed on Linux, but most of the development was done on Mac
OS X.

Waf (see [waf][waf]) is used as the build tool. Waf requires python
2.x to be installed.

Koi depends on a relatively recent version of boost (I recommend
boost 1.50+)

Since koi uses some C++11 language features, you may need to redirect
waf to use clang instead of GCC. This is at least the case when
compiling on Mac OS X.

At the moment, koi is not using any C++11 library
features. Unfortunately the standard library situation on Mac OS X
Mountain Lion and earlier is complicated. GCC 4.2 uses the old C++03
standard library, and the libc++ standard library is only optionally
available for clang. Mixing libraries in one application is
problematic. Basically, koi needs to be compiled using whatever
standard library boost happened to be compiled with. So far, this
still seems to be the older standard library.

Configuring:

    CC=clang CXX=clang++ ./waf configure

Compiling:

    ./waf

## Installation

Compiling koi produces two executables in the `bin/` folder:

* `koinode` is the koi deamon process that should run on each machine
  in the koi cluster.

* `koi` is a client that connects to any `koinode` and executes
  commands to monitor the status of the cluster and perform
  maintenance changes to the cluster.

These should be installed for example in the `/usr/sbin` folder.

The koi configuration file should be placed in `/etc/koi/koi.conf`,
although the configuration path can be changed via the `-f`
command line flag.

By default, koi looks for services to execute in the
`/etc/koi/services` folder. There is no requirement that all nodes
have the same set of services or that services are identical across
nodes.

  [bully]: http://en.wikipedia.org/wiki/Bully_algorithm "Wikipedia"
  [waf]: http://code.google.com/p/waf/ "waf"
  [runit]: http://smarden.org/runit/ "runit"
