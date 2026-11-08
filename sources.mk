# Shared object inventory: included by the standalone GNUmakefile and the
# rustyrig-fw in-tree build. Do not list generated/third-party objects here.
librustyaxe_objs += acl.o
librustyaxe_objs += config.o
librustyaxe_objs += dict.o
ifeq (${USE_EEPROM},true)
librustyaxe_objs += eeprom.o
endif
librustyaxe_objs += event-bus.o
librustyaxe_objs += cfg.modules.o
librustyaxe_objs += cfg.serial.o
librustyaxe_objs += io.o
librustyaxe_objs += io.serial.o
librustyaxe_objs += irc.format.o
librustyaxe_objs += json.o
librustyaxe_objs += list.o
librustyaxe_objs += logger.o
librustyaxe_objs += log-queue.o
librustyaxe_objs += maidenhead.o
librustyaxe_objs += module.o
ifneq ($(LIBRUSTYAXE_STANDALONE),true)
ifeq ($(USE_MONGOOSE),true)
librustyaxe_objs += mongoose.o
endif
endif
librustyaxe_objs += posix.o
librustyaxe_objs += runtime-globals.o
librustyaxe_objs += ringbuffer.o
librustyaxe_objs += scheduler.o
librustyaxe_objs += privileges.o
librustyaxe_objs += socks.o
librustyaxe_objs += subproc.o
librustyaxe_objs += reactor.glib.o
librustyaxe_objs += transport.o
# The native POSIX/OpenSSL/wslay WebSocket client is not part of MCU builds.
ifeq ($(LIBRUSTYAXE_STANDALONE),true)
librustyaxe_objs += transport.wss.o
else ifeq ($(PLATFORM),posix)
librustyaxe_objs += transport.wss.o
endif
librustyaxe_objs += transport.mongoose.o
librustyaxe_objs += termkey.o
librustyaxe_objs += tui.o
librustyaxe_objs += tui.driver-csi.o
librustyaxe_objs += tui.driver-ti.o
librustyaxe_objs += tui.keys.o
librustyaxe_objs += tui.theme.o
librustyaxe_objs += tui.window.o
librustyaxe_objs += tui.completion.o
librustyaxe_objs += util.file.o
librustyaxe_objs += util.math.o
librustyaxe_objs += util.mem.o
librustyaxe_objs += util.string.o
librustyaxe_objs += util.time.o
