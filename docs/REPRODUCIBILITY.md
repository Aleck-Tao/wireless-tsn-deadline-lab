# Reproducibility

The clock-estimator experiment uses `configs/clock-ablation/plan.json` with the same `run-plan` command and a fresh output such as `artifacts/clock-ablation`. It produces 160 runs, the usual validated event/metric files, and `CLOCK_ANALYSIS.md` with paired estimator/margin contrasts. `python -m wtsn_eval verify results/clock-ablation --source-root .` verifies its committed evidence after downloading or cloning the repository. CI reruns both matrices every Monday at 01:23 UTC and on manual dispatch; uploaded artifacts are retained for 90 days. Committed results remain available independently of artifact expiry.

## Clean run

Use CMake 3.20 or newer, a C++20 compiler, and Python 3.10 or newer. Python analysis uses only the standard library. The release artifact's normative execution environment is the workflow's Ubuntu 24.04 GCC/libstdc++ job.

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests/python -v
python -m wtsn_eval run-plan --plan configs/release/plan.json --binary build/wtsn-sim --output artifacts/release --source-root .
python -m wtsn_eval verify artifacts/release --source-root . --binary build/wtsn-sim
```

On a multi-config generator such as Visual Studio, add `--config Release` to the build command and point `--binary` at the resulting configuration directory. The simulation and evaluator contain no platform-specific shell commands.

## Artifact contents

Each leaf run contains `events.csv`, `run_metadata.json`, and independently produced `metrics.json`. The artifact root contains the normalized `benchmark_plan.json`, a CSV/JSON/Markdown summary, paired comparisons, and `provenance.json`. Before aggregation, the evaluator requires exactly one canonical metrics file for every scenario/scheduler/seed tuple in that embedded plan; missing, unexpected, or duplicate identities abort the report.

The C++ writer refuses to overwrite either event or metadata output from an earlier run, and the matrix runner requires an absent or empty artifact directory. To rerun, choose a new directory; do not mutate an existing evidence chain.

The manifest stores SHA-256 for the simulator binary, every result file, and the exact recursive set of source files. Source discovery intentionally excludes `.git`, virtual environments, compiler build trees, `artifacts`, and `results`; it does not rely on a hand-maintained extension allow-list, so a nested source/config/document file cannot silently escape coverage. Verification rejects missing, added, or changed files.

Passing `--binary` makes verification hash that file and reports `binary_verified=true`; CI always uses this mode. A downloaded result artifact does not contain the original executable, so omitting `--binary` verifies source and result file sets but reports `binary_verified=false`: the manifest's simulator hash is then recorded but not independently checked. A rebuilt executable can be compared with `--binary`, although byte identity requires the same compiler, standard library, build flags, and reproducible toolchain behavior.

To prevent a manifest from recursively treating its own output as source, the runner requires outputs inside the repository to live under `artifacts/` or `results/`. An output directory outside the repository is also accepted.

The manifest has no wall-clock timestamp or absolute path. JSON is key-sorted, strict (no NaN/Infinity), UTF-8, and LF-terminated. CSV uses fixed schemas and LF line endings. These choices make run content reviewable across CI downloads. C++ standard-library probability distributions are not specified bit-for-bit across implementations, so identical seeds are not promised to reproduce identical traces under a different standard library; the simulator-binary hash records that distinction.

## Publishing results

The GitHub Actions `full-benchmark` job runs on pushes and manual dispatches, then uploads `wireless-tsn-release-<commit SHA>`. A checked-in result should be copied from that artifact without editing, verified against the same source commit, and committed separately from the model change. This keeps implementation review distinct from empirical output and avoids presenting unexecuted local results.
