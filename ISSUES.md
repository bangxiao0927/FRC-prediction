# Issues

- [x] UI: optimized dashboard layout (tab navigation, zebra-striped tables,
  sticky headers, responsive breakpoints, Top defaults to event team count)
- [x] Online evaluation: `--live` flag polls TBA continuously and tracks
  running MAE / winner accuracy with optional CSV output
- [x] Model tuning: `--tune` flag runs grid search over sigmoid_scale,
  score_diff_scale, and confidence_match_count; reports best by MAE and
  winner accuracy with optional JSON output
