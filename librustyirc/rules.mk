librustyirc := librustyirc.so
libs += ${librustyirc}
librustyirc_objs := irc connection irc.channel irc.modes irc.user irc.capab irc.commands irc.numerics irc.event irc.dispatch irc.audio irc.dcc
librustyirc_headers := $(wildcard librustyaxe/librustyirc/*.h)
librustyirc_real_objs := $(addprefix ${BUILD_DIR}/librustyaxe/librustyirc/,$(addsuffix .o,${librustyirc_objs}))

${BUILD_DIR}/librustyaxe/librustyirc/%.o: librustyaxe/librustyirc/%.c ${librustyirc_headers} ${librustyaxe_headers} GNUmakefile librustyaxe/librustyirc/rules.mk ${OBJECT_ORDER_ONLY}
	@mkdir -p $(dir $@)
	@echo "[compile] $< => $@"
	@${CC} ${CFLAGS} ${CFLAGS_WARN} ${extra_cflags} -o $@ -c $< || exit 2

${librustyirc}: ${librustyirc_real_objs} ${librustyaxe}
	@echo "[link] $@ from $(words ${librustyirc_real_objs}) objects"
	@${CC} ${LDFLAGS} ${LIB_LDFLAGS} $(call SHARED_ID_FLAG,librustyirc.so) -o $@ ${librustyirc_real_objs} -lrustyaxe || exit 2
	@ln -sf librustyirc.so librustyirc.so.0

extra_clean += ${librustyirc} librustyirc.so.0
