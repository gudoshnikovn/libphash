# Batch hashing and threads

`ph_hash_files()` and `ph_hash_buffers()` hash an array of files or in-memory buffers in
one call, across a pool of worker threads. This page is how to use them and what they
guarantee; `include/libphash.h` has every function's full contract.

## How a batch runs

`ph_hash_files()`/`ph_hash_buffers()` hash an array of files or in-memory buffers, each
item loaded and hashed with `ph_compute_multi()` independently, optionally across an
internal pool of worker threads. Each worker creates and owns its own `ph_context_t`,
claims the next unstarted item from a shared atomic index, and writes only into that
item — per-item failures land in the item's `status` and never stop the batch.

`ph_hash_files_ex()`/`ph_hash_buffers_ex()` take a `ph_batch_options_t` (initialize it
with `ph_batch_options_init()`) and add what the plain pair cannot do:

- **Configuration.** `options.config` is a template context: its whole configuration —
  gray weights, algorithm parameters, `max_pixels`, decode scale, auto-orient — is copied
  on the calling thread into every worker's context, so the batch hashes an item exactly
  as that context would. The plain pair always runs on the defaults, including the
  default `max_pixels`, whatever was configured elsewhere.
- **Cancellation.** `options.should_continue` is called before each item; once it
  returns 0 no new item is started, the call returns `PH_ERR_CANCELLED`, and every item
  not started carries `PH_ERR_CANCELLED`. The plain pair blocks until the last item.
- **Progress.** `options.on_progress` is called after each finished item.

Both callbacks run on the worker threads, possibly concurrently, so they must be
thread-safe.

**Memory.** Each worker holds one decoded image at a time: its RGB pixels plus a
grayscale copy, about 4 bytes per pixel. The peak is therefore roughly
`workers × 4 × the largest image's pixel count` — linear in the thread count, and
`threads = 0` means one worker per CPU available to the process. At the default `max_pixels` (256 Mi pixels) that
bound is about 1 GB per worker; measured on 20-megapixel JPEGs it is about 80 MB per
worker (94 MB at one thread, 1.35 GB at sixteen). To bound it, pass an explicit thread
count, a lower `max_pixels` on the template, or both.

**One image, one thread.** Parallelism is across images only: `threads` sizes the
batch's worker pool and nothing else. A single image — in a batch worker or through a
direct `ph_load_*()`/`ph_compute_*()` call — is decoded, converted to gray and hashed on
the calling thread at every stage. Splitting one image across threads would buy little
where the time goes: on a 20-megapixel JPEG the decode is about 85% of a
`ph_compute_multi()` call with all four flags, and neither libjpeg-turbo nor libpng can
split one decode across threads. It would also oversubscribe the machine whenever a
batch already runs one worker per CPU, which is why libwebp's optional second decoding
thread is left off as well. A caller with one large JPEG and idle cores gains more from
`ph_context_set_decode_scale()`.

## Threads: what is safe

- **One context per thread.** Every function is safe to call from any number of threads
  at once as long as each thread uses its own `ph_context_t`. A single context is not
  safe to share: one thread loading into it while another hashes from it is a data race.
  The `tsan` CI job runs the library under ThreadSanitizer with threads each owning a
  context (`tests/src/test_thread_safety.c`).
- **Batch calls take no context**, so several threads may each run their own
  `ph_hash_files()`/`ph_hash_buffers()` at the same time. The calls share nothing but
  the library's one-time initialization, which is thread-safe; each starts its own pool,
  so their worker counts add up — pass explicit thread counts if several run at once.
- **The template context** (`options.config`) is only read, on the calling thread, before
  any worker starts. Do not change it from another thread while the call is starting.
- **Callbacks** (`should_continue`, `on_progress`) run on the worker threads, possibly
  several at once; they must be thread-safe.

## Threads: how many

`threads = 1` runs every item on the calling thread with no thread created. `threads = 0`
uses one worker per CPU this process may use: the online CPUs, narrowed on Linux by the
affinity mask (`taskset`, `docker --cpuset-cpus`) and by a cgroup CPU quota, so a
container limited to two CPUs gets two workers. On Windows `threads = 0` stops at 64, the
size of one processor group. A count larger than the number of items runs one worker per
item. A library built without `PHASH_ENABLE_THREADS` runs every batch sequentially;
`ph_get_build_info()` says which (`threads=on` or `threads=off`).

## What an item costs

An item's time is its decode plus its hashes, and both grow with the **source image's
pixel count**, not with the size of the hash: the decoder produces every pixel, and the
reduction to the hash's working size reads every one of them before the hash proper runs
on a few thousand values. A 20-megapixel photo costs more than a hundred times what
a 400×400 one does. Two settings cut the cost of large JPEGs at the source:
`ph_context_set_decode_scale()` on the template decodes at 1/2, 1/4 or 1/8 of the size,
and `ph_context_set_load_grayscale()` skips the color pass when only grayscale
algorithms are requested. `docs/theory/choosing.md` has measured per-algorithm costs.
