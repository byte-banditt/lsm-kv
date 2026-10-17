# CI and benchmark baseline checklist

Run these steps from top to bottom:

1. Push branch `feat/ci-and-docs` to GitHub.
2. In the `CI` workflow, confirm the Release and ASan+UBSan `build-test` jobs pass. Confirm the `bench` job passes or reports that it skipped the gate because `bench/baseline.json` is absent.
3. Merge the branch to the repository's default branch. The scheduled trigger and `workflow_dispatch` are available only when the workflow exists on the default branch.
4. On the default branch, run workflow `CI` with input `update_baseline=true`.
5. Download artifact `benchmark-baseline-candidate`. Its uploaded file path is `bench-runs/result.json`.
6. Copy the downloaded JSON to `bench/baseline.json`, commit it, and push that commit to the default branch.
7. Confirm `CI` runs again and that its `bench` job compares the five-run median against `bench/baseline.json`.
8. Run workflow `Nightly crash recovery` manually once with `workflow_dispatch` and confirm the 200-iteration test passes.
