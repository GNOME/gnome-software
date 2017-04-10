if ENABLE_TESTS

app-with-runtime/repo/config: $(srcdir)/flatpakrepos.tar.gz
	$(AM_V_GEN) tar -xf $(srcdir)/flatpakrepos.tar.gz

flatpakrepos.tar.gz:
	$(AM_V_GEN) tar -czf $(srcdir)flatpakrepos.tar.gz	\
		app-missing-runtime/repo/		\
		app-update/repo/			\
		app-with-runtime/repo/			\
		only-runtime/repo/

BUILT_SOURCES =						\
	app-with-runtime/repo/config

DISTCLEANFILES =					\
	app-missing-runtime/repo			\
	app-update/repo					\
	app-with-runtime/repo				\
	only-runtime/repo

endif

EXTRA_DIST = flatpakrepos.tar.gz

-include $(top_srcdir)/git.mk
