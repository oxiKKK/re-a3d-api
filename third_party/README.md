# Dependencies

| Dependency | Version / source | License | Enabled by |
| --- | --- | --- | --- |
| GoogleTest | [1.14.0](https://github.com/google/googletest/tree/v1.14.0) | [BSD-3-Clause](googletest/LICENSE) | `A3D_BUILD_TESTS` |
| minimp3 | [lieff/minimp3](https://github.com/lieff/minimp3) | [CC0 / public domain](minimp3/LICENSE) | `A3D_FIXES` |
| liba52 | [a52dec 0.7.4](https://liba52.sourceforge.net/) | [GPL v2](liba52/COPYING) | `A3D_FIXES` |

Tests default to OFF; A3D_FIXES defaults to ON and includes both decoders.
The runtime EnableMP3Decoder and EnableAC3Decoder settings control creation,
not dependency inclusion. minimp3 and liba52 replace the unavailable
proprietary decoders; they are not reconstructions of Aureal's linked libraries.
Without them, the static decoder entry points return failure.

`liba52/config.h` is a project-written replacement for its generated autoconf
header. Keep vendored source and license notices unchanged.
