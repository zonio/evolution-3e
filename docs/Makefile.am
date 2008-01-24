EXTRA_DIST = Doxyfile footer.html header.html index.doc eds-evo.doc evo-sched.doc atta-design.doc design.pdf design.tex eee-sched.doc itip.doc

doxy:
	SRCDIR=$(top_srcdir) OUTDIR=$(builddir) \
	PROJECT="$(PACKAGE_NAME)" VERSION="$(PACKAGE_VERSION)" \
	  doxygen $(srcdir)/Doxyfile
	rm -f html/doxygen.png
