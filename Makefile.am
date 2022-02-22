
AUTOMAKE_OPTIONS = foreign

AM_CXXFLAGS = -std=c++20

bin_PROGRAMS = fdr

fdr_SOURCES = \
	main.cpp \
	fdr_profile.cpp \
	fdr_record.cpp

# fdr_LDFLAGS = -static
fdr_LDADD = -lyaml-cpp -lsystemd

distclean-local:
	rm -rf autom4te.cache
	rm -f aclocal.m4 configure depcomp install-sh Makefile.in missing
	rm -rf config m4 .deps config.h.in~ config.h.in

