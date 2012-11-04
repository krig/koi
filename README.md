# KOI

Cluster management software.

For license information, see LICENSE.

## Introduction

Koi is an internal tool I threw together at Procera after years of
frustration trying to get Pacemaker to do what we wanted. The main
purpose of koi is to maintain a cluster of machines (usually 3 or 5)
where a subset of those machines run _services_. Services usually
consist of a set of bash scripts that start, stop and monitor some
other piece of software. This software only runs in active mode on one
of the machines, with the other nodes functioning as backup
servers. In the event of a monitoring error, the software is stopped
on the faulty node and started on another machine.

This is the first public release of koi, and it is a little rough
around the edges. There isn't much documentation or much of a guide
for setting it up, and the user interface needs work. It is, however,
basically the same version that we are using in production.

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


  [bully]: http://en.wikipedia.org/wiki/Bully_algorithm "Wikipedia"
  [waf]: http://code.google.com/p/waf/ "waf"
