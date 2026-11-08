# librustyirc

MIT-licensed IRC library built alongside MIT-licensed librustyaxe. The source is maintained in this repository, but is a separate ELF shared library and Debian runtime/development pair. It requires librustyaxe; librustyaxe does not require IRC.

From the repository root: `make`, `make check`, `make install`, `make uninstall`. `make deb` builds the four Debian binary packages, and `make src-deb` builds source packaging.
