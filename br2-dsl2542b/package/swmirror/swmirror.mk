################################################################################
#
# swmirror
#
################################################################################

# Pakiet "lokalny": źródła leżą w tym repozytorium, Buildroot niczego nie
# pobiera z sieci, tylko kopiuje (rsync) katalog src/ do output/build/.
SWMIRROR_VERSION = 1.0
SWMIRROR_SITE = $(BR2_EXTERNAL_DSL2542B_PATH)/package/swmirror/src
SWMIRROR_SITE_METHOD = local
SWMIRROR_LICENSE = GPL-2.0+

# $(TARGET_CC) to cross-kompilator z toolchaina Buildroota,
# $(TARGET_CFLAGS)/$(TARGET_LDFLAGS) zawierają m.in. -Os i --sysroot.
define SWMIRROR_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -Wall -Wextra \
		-o $(@D)/swmirror $(@D)/swmirror.c
endef

define SWMIRROR_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/swmirror $(TARGET_DIR)/usr/sbin/swmirror
	$(INSTALL) -D -m 0644 $(SWMIRROR_PKGDIR)/swmirror.conf \
		$(TARGET_DIR)/etc/swmirror.conf
endef

# Wywoływane tylko, gdy system używa BusyBox init / SysV init.
define SWMIRROR_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(SWMIRROR_PKGDIR)/S41swmirror \
		$(TARGET_DIR)/etc/init.d/S41swmirror
endef

$(eval $(generic-package))
