# Third-Party Software Notices

This document contains licensing information for third-party libraries bundled with **libphash** in the `vendor/` directory.

While **libphash** itself is licensed under the **MIT License**, the following components are subject to their respective licenses.

---

## 1. libjpeg-turbo

* **Project:** [https://libjpeg-turbo.org/](https://libjpeg-turbo.org/)
* **License:** IJG License (with the zlib License for the SIMD code)

libphash links libjpeg-turbo's **libjpeg API library** (`jpeg-static`, installed as
`phash_jpeg`). That library is covered by the IJG (Independent JPEG Group) License, whose
text is in `vendor/libjpeg-turbo/README.ijg`; its SIMD code is covered by the zlib
License, which the IJG License subsumes in this context. libjpeg-turbo's Modified
(3-clause) BSD License covers its TurboJPEG API library and build system, which libphash's
binaries do not contain; `vendor/libjpeg-turbo/LICENSE.md` has the full terms.

The IJG License requires this statement in the documentation of any binary that
contains the library:

*This software is based in part on the work of the Independent JPEG Group.*

---

## 2. libpng

* **Project:** [http://www.libpng.org/pub/png/libpng.html](http://www.libpng.org/pub/png/libpng.html)
* **License:** libpng License 2.0

PNG Reference Library License version 2 (`vendor/libpng/LICENSE`, which also carries
the version 1 terms for older libpng releases):

Copyright (c) 1995-2026 The PNG Reference Library Authors.
Copyright (c) 2018-2026 Cosmin Truta.
Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
Copyright (c) 1996-1997 Andreas Dilger.
Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

The software is supplied "as is", without warranty of any kind, express or implied,
including, without limitation, the warranties of merchantability, fitness for a
particular purpose, title, and non-infringement. In no event shall the Copyright owners,
or anyone distributing the software, be liable for any damages or other liability,
whether in contract, tort or otherwise, arising from, out of, or in connection with the
software, or the use or other dealings in the software, even if advised of the
possibility of such damage.

Permission is hereby granted to use, copy, modify, and distribute this software, or
portions hereof, for any purpose, without fee, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you
   wrote the original software. If you use this software in a product, an acknowledgment
   in the product documentation would be appreciated, but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This Copyright notice may not be removed or altered from any source or altered source
   distribution.

---

## 3. spng (Simple PNG)

* **Project:** [https://github.com/randy408/libspng](https://github.com/randy408/libspng)
* **License:** BSD 2-Clause "Simplified" License

Copyright (c) 2018-2023, Randy Shin. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

---

## 4. stb_image.h

* **Project:** [https://github.com/nothings/stb](https://github.com/nothings/stb)
* **License:** Public Domain / MIT / Unlicense
* **Vendored version:** v2.30, **locally modified** (`vendor/stb_image.h`, SHA-256 `b215f40ad1c6babf485a72307f84fa724507415cbd7ed1d8d7c600798d836f43`; as imported from upstream, before the local patch: `f53ea8b6ed181beb245d4c43c97dca0bf03cd1408e4b6485afc605199bbc9d3f`)

This software is dual-licensed to the public domain and under the following license: you are free to use this software under the terms of the MIT license or the Unlicense.

Copyright (c) 2017 Sean Barrett.

**Modifications by the libphash authors:** allocation failures inside the zlib entry points and inside the allocating format probes are made visible through `stbi_failure_reason()` instead of being lost or overwritten. Each change is marked in the file with `/* libphash local patch (not upstream): ... */`; the reasoning is in `docs/development.md`. Both licenses permit modification; the notice above is retained, and this paragraph records the change as required of a modified copy.

---

## 5. libwebp

* **Project:** [https://developers.google.com/speed/webp/](https://developers.google.com/speed/webp/)
* **License:** BSD 3-Clause License

Copyright (c) 2010, Google Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of Google nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

---

## 6. stb_image_resize2.h

* **Project:** [https://github.com/nothings/stb](https://github.com/nothings/stb)
* **License:** Public Domain / MIT / Unlicense
* **Vendored version:** v2.18, **locally modified** (`vendor/stb_image_resize2.h`, SHA-256 `114b2dbef70f85530aa17171029572ce4da2569a333c065e15b5c7100acde37a`; as imported from upstream, before the local patch: `173e654634f6ccaad98f603e686ea212eec1fe8ea6d2a5e5e8056efa10ae3880`)

This software is dual-licensed to the public domain and under the following license: you are free to use this software under the terms of the MIT license or the Unlicense.

Copyright (c) 2023 Jeff Roberts and Jorge L Rodriguez.

**Modifications by the libphash authors:** the out-of-memory paths of `stbir__alloc_internal_mem_and_build_samplers()` / `stbir__free_internal_mem()` are hardened against a crash and several leaks under `STBIR__SEPARATE_ALLOCATIONS`. Each change is marked in the file with `/* libphash local patch (not upstream): ... */`; the reasoning is in `docs/development.md`. Both licenses permit modification; the notice above is retained, and this paragraph records the change as required of a modified copy.

---

## 7. zlib-ng

* **Project:** [https://github.com/zlib-ng/zlib-ng](https://github.com/zlib-ng/zlib-ng)
* **License:** zlib License

(C) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the restrictions listed in the zlib license.

---

### Summary Table

| Library | Directory | License Type |
| --- | --- | --- |
| **libjpeg-turbo** | `vendor/libjpeg-turbo` | IJG (zlib for SIMD) |
| **libpng** | `vendor/libpng` | libpng License 2.0 |
| **libwebp** | `vendor/libwebp` | BSD 3-Clause |
| **spng** | `vendor/spng` | BSD 2-Clause |
| **stb_image** | `vendor/stb_image.h` | Public Domain (MIT) |
| **stb_image_resize2** | `vendor/stb_image_resize2.h` | Public Domain (MIT) |
| **zlib-ng** | `vendor/zlib-ng` | zlib License |

Everything except the two `stb_*.h` files is a git submodule, so its exact revision is
recorded in the repository and `git submodule status` prints it. The two stb headers are
copied into `vendor/` instead, which is why their versions and hashes are written out
above: without them an update leaves no trace. **Both are modified copies** — each carries
a local patch to its out-of-memory handling, marked in the file and explained in
`docs/development.md`, so two hashes are recorded for each: the file as it is here, and
the upstream file it was derived from.
