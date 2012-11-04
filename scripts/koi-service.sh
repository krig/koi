#!/bin/sh
#
# Source this file in koi services to get helper variables.

KOI_OK=0
KOI_ERROR=1

# return from status
# if status returns any of these
# it is taken as truth, if status
# returns KOI_OK, that is taken to
# mean 'ok'
# if status returns ERROR, that means
# failure
KOI_MASTER=90
KOI_SLAVE=91
KOI_STOPPED=92
