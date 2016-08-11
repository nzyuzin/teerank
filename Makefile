TEERANK_VERSION = 3
TEERANK_SUBVERSION = 0
DATABASE_VERSION = 6
STABLE_VERSION = 0

PREVIOUS_VERSION = $(shell expr $(TEERANK_VERSION) - 1)
PREVIOUS_DATABASE_VERSION = $(shell expr $(DATABASE_VERSION) - 1)

CFLAGS += -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -lm -Icore -Icgi -Wall -Werror -std=c89
CFLAGS += -DTEERANK_VERSION=$(TEERANK_VERSION)
CFLAGS += -DTEERANK_SUBVERSION=$(TEERANK_SUBVERSION)
CFLAGS += -DDATABASE_VERSION=$(DATABASE_VERSION)
CFLAGS += -DSTABLE_VERSION=$(STABLE_VERSION)

BUILTINS_SCRIPTS += upgrade
BUILTINS_SCRIPTS += update
BUILTINS_SCRIPTS := $(addprefix teerank-,$(BUILTINS_SCRIPTS))

SCRIPTS = $(BUILTINS_SCRIPTS)

# Each builtin have one C file with main() function in "builtin/"
BUILTINS_BINS = $(addprefix teerank-,$(patsubst builtin/%.c,%,$(wildcard builtin/*.c)))

# There is only one upgrade binary per release
UPGRADE_BIN = teerank-upgrade-$(PREVIOUS_DATABASE_VERSION)-to-$(DATABASE_VERSION)

BINS = $(UPGRADE_BIN) $(BUILTINS_BINS)

CGI = teerank.cgi

$(shell mkdir -p generated)

# Add debugging symbols and optimizations to check for more warnings
debug: CFLAGS += -O -g
debug: CFLAGS_EXTRA = -O -g
debug: $(BINS) $(SCRIPTS) $(CGI)

# Remove assertions and enable optimizations
release: CFLAGS += -DNDEBUG -O2
release: CFLAGS_EXTRA = -DNDEBUG -O2
release: $(BINS) $(SCRIPTS) $(CGI)

#
# Binaries
#

# Object files
core_objs = $(patsubst %.c,%.o,$(wildcard core/*.c))
page_objs = $(patsubst %.c,%.o,$(wildcard cgi/*.c) $(wildcard cgi/page/*.c))

# Header file dependancies
core_headers = $(wildcard core/*.h)
page_headers = $(wildcard cgi/*.h)

$(core_objs): $(core_headers)
$(page_objs): $(page_headers) $(core_headers)

# config.c use version constants defined here
$(core_objs): Makefile

$(BUILTINS_BINS): $(core_objs)
	$(CC) -o $@ $(CFLAGS) $^

$(BUILTINS_BINS): teerank-% : builtin/%.o

#
# Scripts
#

# Script needs to handle configuration variables as well.  The
# necessary code to handle this is generated by a program.  The
# following rule define how to build this program...
build/generate-default-config: build/generate-default-config.o $(core_objs)
	$(CC) -o $@ $(CFLAGS) $^

# This generate a header prepended to every scripts.  This header set
# default values for configuration variables as well as adding the
# current working directory to the PATH.  This is done so that scripts
# will use binaries in the same directory in priority.
#
# Hence doing ./teerank-update for instance will have the desired, as
# it wont use system wide installed binaries (if any).  And doing
# teerank-update in the same directory will use system wide installed
# binaries, as expected too.
generated/script-header.inc.sh: build/generate-default-config
	@echo "#!/bin/sh" >$@
	@echo 'PATH="$$(dirname $$BASH_SOURCE):$$PATH"' >>$@
	./$< >>$@

$(BUILTINS_SCRIPTS): teerank-%: builtin/%.sh

$(SCRIPTS): generated/script-header.inc.sh
	cat $^ >$@ && chmod +x $@

#
# CGI
#

$(CGI): cgi/cgi.o cgi/route.o $(core_objs) $(page_objs)
	$(CC) -o $@ $(CFLAGS) $^

#
# Lib
#

# Header files needs to be prefixed
build/prefix-header: build/prefix-header.o
	$(CC) -o $@ $(CFLAGS) $^

# Upgrade binary needs both current library and the previous one
CURRENT_LIB = libteerank$(DATABASE_VERSION).a
PREVIOUS_LIB = libteerank$(PREVIOUS_DATABASE_VERSION).a

PREVIOUS_BRANCH = teerank-$(PREVIOUS_VERSION).y

$(CURRENT_LIB): $(core_objs)
	ar rcs $@ $^
	./build/prefix-static-library.sh $@ teerank$(DATABASE_VERSION)_

$(PREVIOUS_LIB): dest = .build/teerank$(PREVIOUS_DATABASE_VERSION)
$(PREVIOUS_LIB): .git/refs/heads/$(PREVIOUS_BRANCH) build/prefix-header
	mkdir -p $(dest)
	git archive $(PREVIOUS_BRANCH) | tar x -C $(dest)
	CFLAGS="$(CFLAGS_EXTRA)" $(MAKE) -C $(dest) $(PREVIOUS_LIB)
	cp $(dest)/$(PREVIOUS_LIB) .

	# Prefix every header file in core
	for i in $(dest)/core/*.h; do \
		./build/prefix-header teerank$(PREVIOUS_DATABASE_VERSION)_ <"$$i" >"$$i".tmp; \
		mv "$$i".tmp "$$i"; \
	done

#
# Upgrade
#

UPGRADE_OBJS = $(patsubst %.c,%.o,$(wildcard upgrade/*.c))
$(UPGRADE_OBJS): CFLAGS += -I.build
$(UPGRADE_OBJS): $(PREVIOUS_LIB)
$(UPGRADE_BIN): $(core_objs) $(UPGRADE_OBJS) $(PREVIOUS_LIB)
	$(CC) -o $@ $(CFLAGS) -I.build $^

#
# Clean
#

clean:
	rm -f core/*.o builtin/*.o cgi/*.o cgi/page/*.o build/*.o
	rm -f $(BINS) $(SCRIPTS) $(CGI) $(LIB)
	rm -f $(CURRENT_LIB) $(PREVIOUS_LIB)
	rm -f generated/script-header.inc.sh build/generate-default-config build/prefix-header
	rm -rf generated/ .build/

#
# Install
#

install: TEERANK_ROOT       = $(prefix)/var/lib/teerank
install: TEERANK_DATA_ROOT  = $(prefix)/usr/share/webapps/teerank
install: TEERANK_BIN_ROOT   = $(prefix)/usr/bin
install:
	mkdir -p $(TEERANK_ROOT)
	mkdir -p $(TEERANK_DATA_ROOT)
	mkdir -p $(TEERANK_BIN_ROOT)

	cp $(BINS) $(SCRIPTS) $(TEERANK_BIN_ROOT)
	cp -r $(CGI) assets/* $(TEERANK_DATA_ROOT)

.PHONY: all debug release lib clean install
