SUBDIRS =						\
	data						\
	po						\
	src						\
	tests

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-$(VERSION)-`date +"%Y%m%d"`

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

DISTCLEANFILES =					\
	gnome-software-*.tar.xz

MAINTAINERCLEANFILES =					\
	*~						\
	aclocal.m4					\
	ChangeLog					\
	compile						\
	config.guess					\
	config.h.*					\
	config.sub					\
	configure					\
	depcomp						\
	install-sh					\
	ltmain.sh					\
	Makefile.in					\
	missing						\
	mkinstalldirs					\
	NEWS						\
	test-driver

EXTRA_DIST =						\
	AUTHORS						\
	autogen.sh					\
	config.h					\
	COPYING						\
	MAINTAINERS					\
	NEWS						\
	README

DISTCHECK_CONFIGURE_FLAGS = 				\
	--disable-dogtail

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
		rm -f NEWS; \
	fi

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log 40b7dc172740c49933139d950b72aab5549df92e.. --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

NEWS: data/appdata/*.appdata.xml
	$(AM_V_GEN) \
	if test -e $(APPSTREAM_UTIL); then \
		$(APPSTREAM_UTIL) appdata-to-news $^ > $@; \
	else \
		touch $@; \
	fi

.PHONY: ChangeLog NEWS

-include $(top_srcdir)/git.mk
