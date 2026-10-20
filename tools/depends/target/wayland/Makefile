include ../../Makefile.include WAYLAND-VERSION ../../download-files.include
DEPS =../../Makefile.include Makefile WAYLAND-VERSION  ../../download-files.include

MESON_BUILD_TYPE=release

ifeq ($(DEBUG_BUILD), yes)
  MESON_BUILD_TYPE=debug
endif

# configuration settings
CONFIGURE = $(NATIVEPREFIX)/bin/python3 $(NATIVEPREFIX)/bin/meson setup \
	--prefix $(PREFIX) \
	--libdir $(PREFIX)/lib \
	--buildtype=$(MESON_BUILD_TYPE) \
	-Dscanner=false \
	-Dtests=false \
	-Ddocumentation=false \
	-Ddtd_validation=false

ifeq ($(CROSS_COMPILING), yes)
	CONFIGURE += --cross-file $(PREFIX)/share/cross-file.meson
endif

# Kodi links the TV's libwayland-client, which its EGL uses as well: one copy for both.
# The shared libraries built here are only linked against, not packaged.
ifeq ($(TARGET_PLATFORM),tizen)
	CONFIGURE += -Ddefault_library=shared
	# The TV's library does not export libffi either, Kodi has to link its own.
	CONFIGURE += "-Dc_link_args=['-L$(PREFIX)/lib', '-Wl,--exclude-libs,libffi.a']"
endif

export CC
export CXX
export CFLAGS
export CXXFLAGS
export LDFLAGS

export PKG_CONFIG_LIBDIR=$(PREFIX)/lib/pkgconfig

all: .installed-$(PLATFORM)


.configured-$(PLATFORM): $(DEPS) | $(TARBALLS_LOCATION)/$(ARCHIVE).$(HASH_TYPE)
	rm -rf $(PLATFORM)/*; mkdir -p $(PLATFORM)
	cd $(PLATFORM); $(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	cd $(PLATFORM); rm -rf build; mkdir -p build
	# symlink native wayland scanner pkg-config to allow meson to find
	rm -f $(PREFIX)/lib/pkgconfig/wayland-scanner.pc
	ln -sf $(NATIVEPREFIX)/lib/pkgconfig/wayland-scanner.pc $(PREFIX)/lib/pkgconfig/wayland-scanner.pc
	cd $(PLATFORM); $(CONFIGURE) . build
	touch $@

.installed-$(PLATFORM): .configured-$(PLATFORM)
	cd $(PLATFORM)/build; $(NATIVEPREFIX)/bin/ninja -v
	cd $(PLATFORM)/build; $(NATIVEPREFIX)/bin/ninja -v install

	# symlink native wayland scanner
	rm -f $(PREFIX)/lib/pkgconfig/wayland-scanner.pc
	ln -sf $(NATIVEPREFIX)/lib/pkgconfig/wayland-scanner.pc $(PREFIX)/lib/pkgconfig/wayland-scanner.pc
	touch $@

clean:
	$(MAKE) -C $(PLATFORM) clean
	rm -f .installed-$(PLATFORM) .configured-$(PLATFORM)

distclean:
	rm -rf $(PLATFORM) .installed-$(PLATFORM) .configured-$(PLATFORM)
