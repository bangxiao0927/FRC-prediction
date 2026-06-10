from pathlib import Path

from flask import Flask, jsonify, send_from_directory


BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
WEB_DIR = BASE_DIR / "web"

app = Flask(__name__, static_folder=str(WEB_DIR))


@app.get("/")
def index():
    return send_from_directory(WEB_DIR, "index.html")


@app.get("/api/stats")
def api_stats():
    stats_path = DATA_DIR / "stats.csv"
    if not stats_path.exists():
        return jsonify({"error": "stats.csv not found"}), 404
    return send_from_directory(DATA_DIR, "stats.csv")


@app.get("/api/prediction")
def api_prediction():
    prediction_path = DATA_DIR / "prediction.json"
    if not prediction_path.exists():
        return jsonify({"error": "prediction.json not found"}), 404
    return send_from_directory(DATA_DIR, "prediction.json")


@app.get("/assets/<path:filename>")
def assets(filename: str):
    return send_from_directory(WEB_DIR, filename)


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=True)
