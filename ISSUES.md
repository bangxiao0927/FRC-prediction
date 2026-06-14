# Issues

- [ ] UI: optimize the dashboard layout and responsiveness (spacing, table
  readability, and mobile-friendly view)
- [x] Online evaluation: `--live` flag polls TBA continuously and tracks
  running MAE / winner accuracy with optional CSV output
- [x] Model tuning: `--tune` flag runs grid search over sigmoid_scale,
  score_diff_scale, and confidence_match_count; reports best by MAE and
  winner accuracy with optional JSON output
