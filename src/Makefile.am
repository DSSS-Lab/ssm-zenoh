noinst_HEADERS = ssm-coordinator.h

INCLUDES = $(all_includes) -I$(top_srcdir)/include -I$(top_srcdir)/external/zenoh-c/include


AM_CFLAGS = -Wall -Wno-error=stringop-truncation -Wno-error=format-truncation= # For Linux
AM_CXXFLAGS = $(AM_CFLAGS) $(all_includes) -I../include
AM_LDFLAGS  = -L$(top_srcdir)/external/zenoh-c/target/release

# Build Zenoh before building any target
BUILT_SOURCES = build-zenoh

build-zenoh:
	if [ ! -d $(top_srcdir)/external/zenoh-c/target/release ]; then \
    	  cargo build --manifest-path $(top_srcdir)/external/zenoh-c/Cargo.toml --release \
    fi

## libssm
lib_LTLIBRARIES = libssm.la

libssm_la_SOURCES = libssm-time.c libssm-shm.c libssm.c
libssm_la_CFLAGS = $(AM_CFLAGS) -Werror
libssm_la_CXXFLAGS = $(libssm_la_CFLAGS)
libssm_la_LIBADD = @LD_LIBRT@ -lpthread

#---------------------------------------------------
# ***_la_LDFLAGS = -version-info  CURRENT:REVISION:AGE
# 1. Bugfix                          C   : R++ : A
# 2. Add Interface                   C++ : R=0 : A++
# 3. Delete or Change Interface      C++ : R=0 : A=0

#libssm_la_LDFLAGS = -version-info 5:1:5
libssm_la_LDFLAGS = -version-info 6:0:0

## ssm-coordinator
bin_PROGRAMS = ssm-coordinator

ssm_coordinator: build-zenoh libssm.la

ssm_coordinator_SOURCES = ssm-coordinator.cpp
ssm_coordinator_CFLAGS = $(AM_CFLAGS)
ssm_coordinator_CXXFLAGS = $(ssm_coordinator_CFLAGS)
ssm_coordinator_LDFLAGS = $(AM_LDFLAGS) -Wl,-rpath,'$$ORIGIN/../../external/zenoh-c/target/release'
ssm_coordinator_LDADD = libssm.la -lzenohc

# Program ssm-zenoh
bin_PROGRAMS += ssm-zenoh

ssm_zenoh: build-zenoh libssm.la

ssm_zenoh_SOURCES = ssm-zenoh.cpp
ssm_zenoh_CFLAGS = $(AM_CFLAGS)
ssm_zenoh_CXXFLAGS = $(AM_CXXFLAGS)
ssm_zenoh_LDFLAGS = $(AM_LDFLAGS) -Wl,-rpath,'$$ORIGIN/../../external/zenoh-c/target/release'
ssm_zenoh_LDADD = libssm.la -lzenohc

# Ensure libzenohc.so is installed in /usr/local/lib
install-exec-hook:
	mkdir -p $(DESTDIR)/usr/local/lib
	cp $(top_srcdir)/external/zenoh-c/target/release/libzenohc.so $(DESTDIR)/usr/local/lib/
	ldconfig

# Ensure libzenohc.so is removed on uninstall
uninstall-hook:
	rm -f $(DESTDIR)/usr/local/lib/libzenohc.so
	ldconfig