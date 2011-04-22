moduledir = @MODULEDIR@
AM_LDFLAGS = -module -avoid-version -no-undefined -shared

module_LTLIBRARIES = binary.la posix.la socket.la system.la

binary_la_SOURCES  = binary.cc
binary_la_CXXFLAGS = -Wall -I../
binary_la_LDFLAGS  = $(AM_LDFLAGS)
binary_la_LIBADD   = ../natus/libnatus.la

posix_la_SOURCES  = posix.cc
posix_la_CXXFLAGS = -Wall -I../
posix_la_LDFLAGS  = $(AM_LDFLAGS) -lutil
posix_la_LIBADD   = ../natus/libnatus.la

socket_la_SOURCES  = socket.cc iocommon.cc iocommon.hpp
socket_la_CXXFLAGS = -Wall -I../
socket_la_LDFLAGS  = $(AM_LDFLAGS)
socket_la_LIBADD   = ../natus/libnatus.la

system_la_SOURCES  = system.cc iocommon.cc iocommon.hpp
system_la_CXXFLAGS = -Wall -I../
system_la_LDFLAGS  = $(AM_LDFLAGS)
system_la_LIBADD   = ../natus/libnatus.la
