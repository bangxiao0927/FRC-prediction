from pathlib import Path
import csv
import subprocess
from datetime import datetime, timezone

from flask import Flask, jsonify, request, send_from_directory


BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
WEB_DIR = BASE_DIR / "web"
BIN_PATH = BASE_DIR / "build" / "frc_prediction"
STATS_PATH = DATA_DIR / "stats.csv"
PREDICTION_PATH = DATA_DIR / "prediction.json"

app = Flask(__name__, static_folder=str(WEB_DIR))


@app.get("/")
def index():
    return send_from_directory(WEB_DIR, "index.html")


@app.get("/api/stats")
def api_stats():
    if not STATS_PATH.exists():
        return jsonify({"error": "stats.csv not found"}), 404
    return send_from_directory(DATA_DIR, STATS_PATH.name)


@app.get("/api/prediction")
def api_prediction():
    if not PREDICTION_PATH.exists():
        return jsonify({"error": "prediction.json not found"}), 404
    return send_from_directory(DATA_DIR, PREDICTION_PATH.name)


@app.post("/api/prediction/run")
def api_run_prediction():
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    match_key = str(payload.get("match_key", "")).strip()
    top = payload.get("top", 20)
    use_history = bool(payload.get("use_history", False))
    history_teams = str(payload.get("history_teams", "")).strip()

    try:
        top_count = max(1, min(int(top), 100))
    except (TypeError, ValueError):
        top_count = 20

    if not event_key:
        return jsonify({"error": "event_key is required"}), 400

    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    phase = infer_stats_phase(match_key)
    stats_args = [
        "--event", event_key,
        "--stats",
        "--top", str(top_count),
        "--stats-csv", str(STATS_PATH),
        "--phase", phase
    ]
    # When a specific match is selected, show stats as of just before it so the
    # whole dashboard reflects "up to that match".
    if match_key:
        stats_args.extend(["--before", match_key])
    stats_result = run_cli(stats_args)
    if stats_result.returncode != 0:
        return cli_error_response("Failed to generate team stats.", stats_result)

    predict_args = [
        "--event", event_key,
        "--json",
        "--output", str(PREDICTION_PATH)
    ]
    if match_key:
        predict_args.extend(["--predict", match_key])
    else:
        predict_args.append("--predict-upcoming")
    # Cross-event history is opt-in (extra TBA calls). --history-teams scopes it
    # to specific robots and implies --use-history on the CLI side.
    if history_teams:
        predict_args.extend(["--history-teams", history_teams])
    elif use_history:
        predict_args.append("--use-history")

    prediction_result = run_cli(predict_args)
    if prediction_result.returncode != 0:
        hint = ""
        if "No upcoming match found" in prediction_result.stderr:
            hint = "This event has no upcoming matches. Enter a specific match key, for example 2024casj_qm1."
        return cli_error_response("Failed to generate prediction.", prediction_result, hint)

    prediction = read_prediction_json()
    return jsonify({
        "event_key": event_key,
        "match_key": match_key,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "stats": read_stats_rows(),
        "prediction": prediction,
        "match_team_stats": collect_match_team_stats(event_key, phase, prediction, match_key)
    })


@app.get("/assets/<path:filename>")
def assets(filename: str):
    return send_from_directory(WEB_DIR, filename)


@app.post("/api/picklist/run")
def api_run_picklist():
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    team_key = str(payload.get("team_key", "")).strip()
    strategy = str(payload.get("strategy", "balanced")).strip() or "balanced"
    exclude = str(payload.get("exclude", "")).strip()
    before = str(payload.get("before", "")).strip()
    top = payload.get("top", 24)

    try:
        top_count = max(1, min(int(top), 100))
    except (TypeError, ValueError):
        top_count = 24

    if not event_key:
        return jsonify({"error": "event_key is required"}), 400
    if not team_key:
        return jsonify({"error": "team_key is required"}), 400
    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    args = [
        "--event", event_key,
        "--picklist", team_key,
        "--json",
        "--top", str(top_count),
        "--strategy", strategy
    ]
    if exclude:
        args.extend(["--exclude", exclude])
    if before:
        args.extend(["--before", before])

    result = run_cli(args)
    if result.returncode != 0:
        return cli_error_response("Failed to build picklist.", result)

    try:
        picklist = app.json.loads(result.stdout)
    except ValueError:
        return jsonify({"error": "Could not parse picklist output.",
                        "stdout": result.stdout.strip()}), 500
    return jsonify(picklist)


@app.post("/api/roles/run")
def api_run_roles():
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    before = str(payload.get("before", "")).strip()
    phase = str(payload.get("phase", "all")).strip() or "all"
    top = payload.get("top", 30)

    try:
        top_count = max(1, min(int(top), 200))
    except (TypeError, ValueError):
        top_count = 30

    if not event_key:
        return jsonify({"error": "event_key is required"}), 400
    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    args = [
        "--event", event_key,
        "--roles",
        "--json",
        "--top", str(top_count),
        "--phase", phase
    ]
    if before:
        args.extend(["--before", before])

    result = run_cli(args)
    if result.returncode != 0:
        return cli_error_response("Failed to compute team roles.", result)

    try:
        roles = app.json.loads(result.stdout)
    except ValueError:
        return jsonify({"error": "Could not parse roles output.",
                        "stdout": result.stdout.strip()}), 500
    return jsonify({"event_key": event_key, "phase": phase, "roles": roles})


@app.post("/api/alliance/run")
def api_run_alliance():
    payload = request.get_json(silent=True) or {}
    event_key = str(payload.get("event_key", "")).strip()
    alliance = str(payload.get("alliance", "")).strip()
    vs = str(payload.get("vs", "")).strip()

    if not event_key:
        return jsonify({"error": "event_key is required"}), 400
    if not alliance:
        return jsonify({"error": "alliance is required (e.g. frc254,frc1678,frc604)"}), 400
    if not BIN_PATH.exists():
        return jsonify({"error": "build/frc_prediction not found. Run cmake --build build first."}), 500

    args = [
        "--event", event_key,
        "--alliance", alliance,
        "--json"
    ]
    if vs:
        args.extend(["--vs", vs])

    result = run_cli(args)
    if result.returncode != 0:
        return cli_error_response("Failed to evaluate alliance.", result)

    try:
        evaluation = app.json.loads(result.stdout)
    except ValueError:
        return jsonify({"error": "Could not parse alliance output.",
                        "stdout": result.stdout.strip()}), 500
    return jsonify(evaluation)


def run_cli(args):
    return subprocess.run(
        [str(BIN_PATH), *args],
        cwd=BASE_DIR,
        capture_output=True,
        text=True,
        timeout=60,
        check=False
    )


def infer_stats_phase(match_key):
    """Pick the stats filter that matches how the CLI scores this match.

    Qualification matches use qualification-only stats; elimination matches use
    qualification plus played elimination matches. Mirrors normalize_match_key
    in the CLI so the dashboard table lines up with the prediction.
    """
    key = (match_key or "").strip().lower()
    if not key:
        return "qm"  # upcoming matches are typically qualification
    token = key.split("_")[-1]  # drop the event prefix if present
    if token.isdigit() or token.startswith("qm"):
        return "qm"
    if token.startswith(("qf", "sf", "f")):
        return "elim"
    return "qm"

def cli_error_response(message, result, hint=""):
    return jsonify({
        "error": message,
        "hint": hint,
        "stdout": result.stdout.strip(),
        "stderr": result.stderr.strip()
    }), 400


def read_stats_rows():
    with STATS_PATH.open(newline="") as stats_file:
        return list(csv.DictReader(stats_file))


def read_prediction_json():
    return app.json.loads(PREDICTION_PATH.read_text())


def collect_match_team_stats(event_key, phase, prediction, before=""):
    """Return per-team stats for the predicted match's teams.

    Uses a wide --stats-json pull so the match's teams are always available,
    even when they rank outside the dashboard table's Top N.
    """
    teams = list(prediction.get("red_teams", [])) + list(prediction.get("blue_teams", []))
    if not teams:
        return {}

    args = [
        "--event", event_key,
        "--stats-json",
        "--top", "1000",
        "--phase", phase
    ]
    if before:
        args.extend(["--before", before])
    result = run_cli(args)
    if result.returncode != 0:
        return {}

    try:
        rows = app.json.loads(result.stdout)
    except ValueError:
        return {}

    by_team = {row.get("team_key"): row for row in rows}
    return {team: by_team[team] for team in teams if team in by_team}

if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5001, debug=True)
