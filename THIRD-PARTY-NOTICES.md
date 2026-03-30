# Third-Party Software Notices

This document contains licensing information for third-party libraries bundled with **libphash** in the `vendor/` directory.

While **libphash** itself is licensed under the **MIT License**, the following components are subject to their respective licenses.

---

## 1. libjpeg-turbo

* **Project:** [https://libjpeg-turbo.org/](https://libjpeg-turbo.org/)
* **License:** IJG, Modified BSD, and zlib

`libjpeg-turbo` is covered by three compatible licenses:

1. **The IJG (Independent JPEG Group) License**: This applies to the original libjpeg code.
2. **The Modified (3-clause) BSD License**: This applies to the TurboJPEG API and most of the SIMD extensions.
3. **The zlib License**: This applies to the libjpeg-turbo SIMD extensions based on the work of the Independent JPEG Group.

*Notice:* This software is based in part on the work of the Independent JPEG Group.

---

## 2. libpng

* **Project:** [http://www.libpng.org/pub/png/libpng.html](http://www.libpng.org/pub/png/libpng.html)
* **License:** libpng License 2.0

Copyright (c) 1995-2022 The PNG Reference Library Authors.
Copyright (c) 2018-2022 Cosmin Truta.
Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
Copyright (c) 1996-1997 Andreas Dilger.
Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

The libpng software is provided "AS IS", without warranty of any kind, express or implied.

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

This software is dual-licensed to the public domain and under the following license: you are free to use this software under the terms of the MIT license or the Unlicense.

Copyright (c) 2017 Sean Barrett.

## 5. libwebp

* **Project:** [https://developers.google.com/speed/webp/](https://developers.google.com/speed/webp/)
* **License:** BSD 3-Clause License

Copyright (c) 2010, Google Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of Google nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

---

### Summary Table

| Library | Directory | License Type |
| --- | --- | --- |
| **libjpeg-turbo** | `vendor/libjpeg-turbo` | IJG / BSD-3 / zlib |
| **libpng** | `vendor/libpng` | libpng License 2.0 |
| **libwebp** | `vendor/libwebp` | BSD 3-Clause |
| **spng** | `vendor/spng` | BSD 2-Clause |
| **stb_image** | `vendor/stb_image.h` | Public Domain (MIT) |
