/* SPDX-License-Identifier: MIT */
#ifndef LIBRUSTYAXE_PUBLIC_BUILD_CONFIG_H
#define LIBRUSTYAXE_PUBLIC_BUILD_CONFIG_H

/*
 * Configuration of the installed RustyAxe SDK, not the consuming program.
 * Standalone builds never bundle Mongoose. RustyRig's own build_config.h
 * controls optional application-side transports independently.
 *
 * No public RustyAxe header requires ext/libmongoose/mongoose.h.
 */
#define LIBRUSTYAXE_HAVE_MONGOOSE 0
#define LIBRUSTYAXE_PUBLIC_MONGOOSE_HEADERS 0

#endif
