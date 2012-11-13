#!/usr/bin/make -f

SELF_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

all: all-progress

all-progress:
	@$(SELF_DIR)/waf build

configure:
	@$(SELF_DIR)/waf configure

test:
	@$(SELF_DIR)/waf --alltests

install:
	@$(SELF_DIR)/waf install

uninstall:
	@$(SELF_DIR)/waf uninstall

clean:
	@$(SELF_DIR)/waf clean

distclean:
	@$(SELF_DIR)/waf distclean

check:
	@$(SELF_DIR)/waf check

dist:
	@$(SELF_DIR)/waf dist

.PHONY: clean dist distclean check uninstall install all

