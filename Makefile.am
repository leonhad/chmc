ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src

EXTRA_DIST = \
	BUGS \
	README.md

.PHONY: deb
deb: all
	@if test -z "$(DPKG)" || test -z "$(DPKG_DEB)"; then \
		echo "dpkg and dpkg-deb are required to build a Debian package"; \
		exit 1; \
	fi
	@set -e; \
	package_dir=$$(mktemp -d); \
	trap 'rm -rf "$$package_dir"' 0; \
	architecture=$$($(DPKG) --print-architecture); \
	mkdir -p "$$package_dir/DEBIAN" "$$package_dir/usr/bin" "$$package_dir/usr/share/doc/$(PACKAGE)"; \
	install -m 755 "$(top_builddir)/src/chmc" "$$package_dir/usr/bin/chmc"; \
	install -m 644 "$(top_srcdir)/README.md" "$$package_dir/usr/share/doc/$(PACKAGE)/README.md"; \
	install -m 644 "$(top_srcdir)/COPYING" "$$package_dir/usr/share/doc/$(PACKAGE)/copyright"; \
	printf '%s\n' \
		'Package: $(PACKAGE)' \
		'Version: $(VERSION)' \
		"Architecture: $$architecture" \
		'Maintainer: Leonardo Alves da Costa <leonhad@gmail.com>' \
		'Section: utils' \
		'Priority: optional' \
		'Description: Build Microsoft Compiled HTML Help files' \
		> "$$package_dir/DEBIAN/control"; \
	$(DPKG_DEB) --build "$$package_dir" "$(PACKAGE)_$(VERSION)_$${architecture}.deb"