# Benchmarks

`ckey-bench` is the renderer–decoder test harness from specification §5. It generates symbols with the real encoder and native raster, places each one in a cluttered camera frame, degrades the frame, decodes it with the real reader pipeline, and measures:

- payload recovery;
- corrected Reed–Solomon bytes;
- latency percentiles;
- the smallest side that decodes in at least 90% of trials;
- payload bytes per 1,000 px² at that side;
- false positives on frames that contain no symbol.

Every symbology gets the same frames. Each symbol's side includes its own quiet zone, so symbols are compared at equal area (spec §5: "recoverable payload per physical area").

| File | What it is |
|---|---|
| `ckey-synthetic-quick.md` / `.json` | `ckey-bench --quick` run in the development container: CKey only, since zxing-cpp is not installed there |
| CI artifact `benchmarks` | the same harness built with `-DCKEY_BENCH_ZXING=ON`, adding QR Code (level M) and Data Matrix through zxing-cpp |

**These are synthetic results, not real-camera measurements.** They are not evidence of a density or speed advantage, and none may be quoted as one (spec §5). Real-device latency needs a target defined by device, image size, profile and percentile (spec §6), measured on a labelled capture corpus. That corpus does not exist yet.

```sh
cmake -S . -B build -DCKEY_BENCH_ZXING=ON   # needs zxing-cpp (e.g. apt install libzxing-dev)
cmake --build build --target ckey-bench
./build/ckey-bench --quick --md out.md --json out.json
```
