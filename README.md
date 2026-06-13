# FRC Prediction

FRC Prediction is a data-driven assistant for FRC events. The goal is to deliver real-time win probability predictions during qualification matches and provide picklist/alliance recommendations for elimination and alliance selection.

**Languages:** English (default), [中文说明](README.zh-CN.md).

## Core Goals

- **Qualification win probability**: Predict win/loss and estimated margin based on event stats, team form, and alliance composition.
- **Elimination performance modeling**: Estimate alliance strength by combining team averages, consistency, and complementary roles.
- **Picklist recommendations**: Rank candidate teams based on strategic fit and predicted alliance performance.
- **Extensible data pipeline**: Pull official data from The Blue Alliance (TBA) and allow future scouting inputs.

## Tech Stack

- **Language**: C++17
- **Build**: CMake
- **Dependency manager**: vcpkg (manifest mode)
- **HTTP**: `cpr`
- **JSON**: `nlohmann/json`

We use C++ for performance and real-time compute needs, and vcpkg to simplify third-party libraries on macOS.

## Data Sources

- The Blue Alliance API: events, teams, matches, rankings, and scores
- Future extension: on-site scouting data (scoring ability, defense, errors, reliability)

## Modeling Scope

**No future-data leakage.** A prediction for a match only uses matches scheduled
strictly before it (qualification predictions use earlier qualification matches;
elimination predictions add played elimination matches). This means a team's
average and match count reflect what was known at match time, so backtests
(`--evaluate`) and live predictions use the exact same information.

### Qualification Stage

Inputs:

- Event-specific team performance (completed matches)
- Recent historical team performance
- Red/blue alliance composition
- Schedule and score data from TBA

Outputs:

- Red/blue win probability
- Estimated score margin
- Confidence hints when sample size is small

### Elimination / Picklist Stage

Inputs:

- Qualification averages, consistency, and trends
- Tactical roles (offense/defense/endgame/special tasks)
- Estimated alliance synergy

Outputs:

- Picklist ranking
- Alliance score estimates
- Match win probability

The CLI `--picklist` ranks teams from qualification play using a weighted mix of
strength (average score), consistency (low score variance), and trend (recent vs
early improvement), damped by how many matches a team has played. Use
`--strategy balanced|offense|consistency` to change the mix, `--exclude` to drop
already-picked teams, and `--before MATCH_KEY` to rank as of a point in the event.

## Project Structure

```text
.
├── CMakeLists.txt
├── PLAN.md
├── README.md
├── README.zh-CN.md
├── config.example.json
├── src/
│   └── main.cpp
└── vcpkg.json
```

## Quick Start

### 1. Install prerequisites

Install CMake if needed:

```bash
brew install cmake
```

Install vcpkg:

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg
```

Add `export VCPKG_ROOT=~/vcpkg` to your shell profile (e.g. `~/.zshrc`).

### 2. Configure TBA API Key

Option A: environment variable

```bash
export TBA_AUTH_KEY=your_key_here
```

Option B: local config file

```bash
cp config.example.json config.json
```

Edit `config.json`:

```json
{
  "tba_auth_key": "your_key_here",
  "default_event_key": "2024casj",
  "cache_dir": "data/cache",
  "cache_ttl_seconds": 60,
  "confidence_match_count": 6,
  "score_diff_scale": 30.0,
  "sigmoid_scale": 1.0,
  "model_version": "baseline-v1"
}
```

`config.json` is ignored by Git to avoid leaking API keys.
Responses are cached under `cache_dir` for a short TTL to reduce duplicate API calls.

### 3. Build

```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

### 4. Run

```bash
./build/frc_prediction --status
```

### Web Dashboard (Flask)

1. Build the CLI (the dashboard calls `build/frc_prediction`):

```bash
cmake --build build
```

2. Start the web server:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python app.py
```

3. Open `http://127.0.0.1:5001`, enter an event key (and optionally a match),
   then click **Run**. The dashboard runs the CLI for you — no need to generate
   files by hand.

The dashboard provides:

- A **win-probability bar** plus a red-left / blue-right comparison of each
  alliance (estimated score, win probability, confidence, average matches).
- A **Team Stats** table and chart for the event; the selected match's teams are
  highlighted (red/blue) and listed above the chart with their averages.
- **Match shorthand**: type `3`, `qm3`, or `sf2m1` instead of the full key.
- **Auto-refresh** with a configurable interval to keep predictions current
  during a live event.
- A **Picklist** section: pick a strategy (balanced/offense/consistency), exclude
  already-picked teams, and build a ranked table + chart. It reuses the Event and
  Match fields (Match acts as an "as of" cutoff).

Examples:

```bash
./build/frc_prediction --event 2024casj --matches
./build/frc_prediction --event 2024casj --rankings
./build/frc_prediction --event 2024casj --teams
./build/frc_prediction --event 2024casj --stats
./build/frc_prediction --event 2024casj --stats --top 10
./build/frc_prediction --event 2024casj --stats-json --top 10
./build/frc_prediction --event 2024casj --stats --top 10 --stats-csv data/stats.csv
./build/frc_prediction --event 2024casj --predict 2024casj_qm1
./build/frc_prediction --event 2024casj --predict-upcoming
./build/frc_prediction --event 2024casj --predict-upcoming --json
./build/frc_prediction --event 2024casj --predict-upcoming --json --output data/prediction.json
./build/frc_prediction --event 2024casj --evaluate
./build/frc_prediction --event 2024casj --evaluate --phase qm
./build/frc_prediction --event 2024casj --evaluate --phase elim --eval-json data/eval.json
./build/frc_prediction --event 2024casj --evaluate --phase all --eval-csv data/eval.csv
./build/frc_prediction --event 2024casj --picklist frc254 --top 24 --strategy balanced
./build/frc_prediction --event 2024casj --picklist --top 24
./build/frc_prediction --event 2024casj --picklist --strategy offense --exclude 1678,254
./build/frc_prediction --event 2024casj --picklist --before qm40 --json
./build/frc_prediction --event 2024casj --picklist --picklist-csv data/picklist.csv

Default prediction output path when using --json without --output:

```
data/predictions/<match_key>.json
```

Prediction outputs now include team counts, average matches per alliance, and adjusted averages relative to the event.
Qualification predictions use qualification matches only; elimination predictions use qualification plus played elimination matches.
```

## Roadmap

- [x] Initialize CMake + vcpkg project
- [x] Add TBA API minimal request
- [x] Add local config template
- [ ] Wrap a `TbaClient` class
- [ ] Pull event matches and rankings
- [ ] Add local cache
- [ ] Compute baseline team statistics
- [ ] Generate qualification win probabilities
- [ ] Produce elimination picklist recommendations

## Short-Term TODO

- Define `TbaClient` and move request logic out of `main.cpp`
- Add `EventData` / `TeamStats` / `MatchPrediction` data structures
- Draft a baseline scoring model (avg, consistency, trend, alliance synergy)
- Add CLI args such as `--event 2024casj` and `--match qm1`

## Contributing

This project is early-stage. The first milestone is an MVP that can pull one event and provide explainable win probabilities for qualification matches, then expand to picklist and elimination predictions.

## Picklist Strategy (How Ranking Is Computed)

Picklist recommendations are built to maximize alliance fit rather than raw strength.

### Inputs

- Per-team average score from qualification-only matches (by default).
- Standard deviation of match scores (consistency).
- Recent trend from the last three completed matches.
- Your team key (to compute complement/overlap).

### Scores

For each candidate team:

```
strength    = candidate_avg / event_avg
consistency = 1 / (1 + stddev)
trend       = (recent_avg - candidate_avg) / event_avg

if my_avg >= event_avg:
  complement = event_avg / (candidate_avg + event_avg)  # favors support roles
else:
  complement = candidate_avg / (candidate_avg + event_avg)  # favors scoring partners

overlap_penalty = max(0, 1 - abs(candidate_avg - my_avg) / event_avg)

picklist_score =
  w_strength * strength
  + w_consistency * consistency
  + w_trend * trend
  + w_complement * complement
  - w_overlap * overlap_penalty
```

Strategy presets map to different weights:

- **balanced**: 0.45 strength, 0.25 consistency, 0.10 trend, 0.25 complement, 0.15 overlap
- **offense**: 0.60 strength, 0.15 consistency, 0.10 trend, 0.30 complement, 0.10 overlap
- **consistency**: 0.30 strength, 0.50 consistency, 0.10 trend, 0.30 complement, 0.10 overlap

### Usage

```
./build/frc_prediction --event 2024casj --picklist frc254 --top 24 --strategy balanced
```
