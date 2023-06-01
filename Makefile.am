
AUTOMAKE_OPTIONS = foreign

#AM_CXXFLAGS = -std=c++20
#AM_CXXFLAGS = -std=gnu++2a
AM_CXXFLAGS = -std=c++2a


bin_PROGRAMS = fdr

fdr_SOURCES = \
	main.cpp \
	fdr_policy.cpp \
	fdr_record.cpp \
	fdr_store.cpp \
	fdr_compact.cpp \
	fdr_bookoferrors.cpp \
	fdr_logs_schema.pb.cc

# fdr_LDFLAGS = -static
fdr_LDADD = -lyaml-cpp -lsystemd -lprotobuf

distclean-local:
	rm -rf autom4te.cache
	rm -f aclocal.m4 configure depcomp install-sh Makefile.in missing
	rm -rf config m4 .deps config.h.in~ config.h.in

