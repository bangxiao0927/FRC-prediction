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

Examples:

```bash
./build/frc_prediction --event 2024casj --matches
./build/frc_prediction --event 2024casj --rankings
./build/frc_prediction --event 2024casj --teams
./build/frc_prediction --event 2024casj --stats
./build/frc_prediction --event 2024casj --stats --top 10
./build/frc_prediction --event 2024casj --stats-json --top 10
./build/frc_prediction --event 2024casj --predict 2024casj_qm1
./build/frc_prediction --event 2024casj --predict-upcoming
./build/frc_prediction --event 2024casj --predict-upcoming --json
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
