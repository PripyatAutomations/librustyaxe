ifeq ($(OS),DARWIN)
AXE_TERM_PKG := ncurses
AXE_TERM_LDLIB := -lncurses
else
AXE_TERM_PKG := tinfo
AXE_TERM_LDLIB := -ltinfo
endif
CFLAGS +=$(call pkgconfig, --cflags $(AXE_TERM_PKG) glib-2.0 gio-2.0)
LDFLAGS +=$(call pkgconfig, --libs $(AXE_TERM_PKG) glib-2.0 gio-2.0)

librustyaxe ?= ../librustyaxe.so
# Keep this inventory owned by librustyaxe, not the RustyRig parent.
include librustyaxe/sources.mk

# The public headers live in the source tree today.  Track them explicitly so
# changing a shared struct (for example http_user_t) rebuilds every consumer
# instead of leaving an ABI-mismatched shared object behind.
librustyaxe_headers := $(wildcard librustyaxe/*.h) $(wildcard inc/librustyaxe/*.h)
librustyaxe_src = $(wildcard librustyaxe/*.c)

real_librustyaxe_objs := $(foreach x, ${librustyaxe_objs}, ${BUILD_DIR}/librustyaxe/${x})
extra_clean += ${real_librustyaxe_objs} ${librustyaxe} librustyaxe.so.0

libs += ${librustyaxe}

${librustyaxe_src}: GNUmakefile ${librustyaxe_headers} librustyaxe/rules.mk ${BUILD_DIR}/build_config.h

${BUILD_DIR}/librustyaxe/.timestamp:
	mkdir -p ${BUILD_DIR}/librustyaxe
	@touch ${BUILD_DIR}/librustyaxe/.timestamp

${librustyaxe}: ${BUILD_DIR}/librustyaxe/.timestamp ${real_librustyaxe_objs} ${librustyaxe_headers} GNUmakefile
	@${RM} -f $@
	@echo "[link] $@ from $(words ${real_librustyaxe_objs}) objects"
	@${CC} ${LIB_LDFLAGS} $(call SHARED_ID_FLAG,librustyaxe.so) -o $@ ${real_librustyaxe_objs}  -lm $(AXE_TERM_LDLIB) ${LDFLAGS}|| exit 2
	@ln -sf librustyaxe.so librustyaxe.so.0

# Every object includes build_config.h (via librustyaxe headers), so it must
# wait for pack-eeprom to generate it.  Order-only avoids relinking on every
# pack-eeprom run.
${BUILD_DIR}/librustyaxe/%.o:librustyaxe/%.c GNUmakefile ${librustyaxe_headers} ${OBJECT_ORDER_ONLY}
	@${RM} $@
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS} -o $@ -c $< || exit 2

${BUILD_DIR}/librustyaxe/mongoose.o:ext/libmongoose/mongoose.c GNUmakefile ${librustyaxe_headers} ${OBJECT_ORDER_ONLY}
	@${RM} $@
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS} -o $@ -c $< || exit 2
