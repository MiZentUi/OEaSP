#!/usr/bin/env python3
"""Run LR5 HTTP experiments and store raw client summaries in CSV."""
import csv
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
CLIENT = BUILD / "client"
SERVER = BUILD / "server"
REPORT_DATA = ROOT / "report" / "data"
RAW = REPORT_DATA / "raw"
RESULTS = REPORT_DATA / "results.csv"
HOST = "127.0.0.1"
PORT = "20103"
REPEATS = 3

FIELDS = [
    "experiment", "case", "run", "method", "path", "size", "clients",
    "count", "total_ms", "requests", "success", "errors", "timeouts",
    "status_2xx", "status_4xx", "sent_bytes", "recv_bytes",
    "avg_request_ms", "avg_connect_ms", "avg_ttfb_ms",
    "throughput_kib_s", "exit_code",
]


def parse_summary(output):
    for line in output.splitlines():
        if not line.startswith("SUMMARY "):
            continue
        values = {}
        for part in line.split()[1:]:
            if "=" not in part:
                continue
            key, value = part.split("=", 1)
            values[key] = value
        return values
    raise RuntimeError("SUMMARY not found in client output:\n" + output)


def safe_name(text):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text)


def save_raw(name, command, result):
    path = RAW / (safe_name(name) + ".log")
    with path.open("w", encoding="utf-8") as stream:
        stream.write("$ " + " ".join(command) + "\n")
        stream.write("exit=" + str(result.returncode) + "\n")
        stream.write("--- stdout ---\n")
        stream.write(result.stdout)
        stream.write("--- stderr ---\n")
        stream.write(result.stderr)


def run_client(name, path, method="GET", count=1, timeout_ms=5000,
               port=PORT, allow_failure=False):
    command = [
        str(CLIENT), "--quiet", "--host", HOST, "--port", str(port),
        "--method", method, "--path", path, "--count", str(count),
        "--timeout-ms", str(timeout_ms), "--id", "1",
    ]
    result = subprocess.run(command, text=True, capture_output=True,
                            check=False)
    save_raw(name, command, result)
    summary = parse_summary(result.stdout)
    if result.returncode != 0 and not allow_failure:
        raise RuntimeError("client failed:\n" + result.stderr)
    return summary, result.returncode


def base_row(experiment, case, run, summary, exit_code):
    return {
        "experiment": experiment,
        "case": case,
        "run": run,
        "method": summary.get("method", ""),
        "path": summary.get("path", ""),
        "size": "",
        "clients": 1,
        "count": summary.get("requests", ""),
        "total_ms": summary.get("total_ms", ""),
        "requests": summary.get("requests", ""),
        "success": summary.get("success", ""),
        "errors": summary.get("errors", ""),
        "timeouts": summary.get("timeouts", ""),
        "status_2xx": summary.get("status_2xx", ""),
        "status_4xx": summary.get("status_4xx", ""),
        "sent_bytes": summary.get("sent_bytes", ""),
        "recv_bytes": summary.get("recv_bytes", ""),
        "avg_request_ms": summary.get("avg_request_ms", ""),
        "avg_connect_ms": summary.get("avg_connect_ms", ""),
        "avg_ttfb_ms": summary.get("avg_ttfb_ms", ""),
        "throughput_kib_s": summary.get("throughput_kib_s", ""),
        "exit_code": exit_code,
    }


def start_server():
    command = [
        str(SERVER), "--host", HOST, "--port", PORT, "--quiet",
        "--timeout-ms", "5000", "--slow-ms", "50",
    ]
    log = (REPORT_DATA / "server_experiments.log").open("w",
                                                       encoding="utf-8")
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                               text=True)
    for attempt in range(50):
        probe = subprocess.run(
            [str(CLIENT), "--quiet", "--port", PORT, "--path", "/empty",
             "--timeout-ms", "200"],
            text=True, capture_output=True, check=False)
        if probe.returncode == 0:
            log.close()
            return process
        if process.poll() is not None:
            log.close()
            raise RuntimeError("server exited during startup")
        time.sleep(0.05)
    process.terminate()
    log.close()
    raise RuntimeError("server did not start")


def stop_server(process):
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def size_experiment(rows):
    cases = [
        ("empty", "/empty", 0),
        ("missing", "/missing", 14),
        ("small", "/small", 128),
        ("medium", "/medium", 8192),
        ("large", "/large", 131072),
    ]
    for case, path, size in cases:
        for run in range(1, REPEATS + 1):
            name = f"size_{case}_run{run}"
            summary, code = run_client(name, path, count=100)
            row = base_row("size", case, run, summary, code)
            row["size"] = size
            rows.append(row)


def method_experiment(rows):
    for method in ("GET", "HEAD"):
        for run in range(1, REPEATS + 1):
            name = f"method_{method.lower()}_run{run}"
            summary, code = run_client(name, "/large", method=method,
                                       count=100)
            row = base_row("method", method, run, summary, code)
            row["size"] = 131072
            rows.append(row)


def parallel_run(name, clients, count, run):
    commands = []
    for index in range(clients):
        commands.append([
            str(CLIENT), "--quiet", "--host", HOST, "--port", PORT,
            "--method", "GET", "--path", "/medium", "--count", str(count),
            "--timeout-ms", "5000", "--id", str(index + 1),
        ])
    started = time.monotonic()
    processes = [
        subprocess.Popen(command, text=True, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
        for command in commands
    ]
    outputs = [process.communicate() for process in processes]
    wall_ms = (time.monotonic() - started) * 1000.0

    summaries = []
    log_path = RAW / (safe_name(name) + ".log")
    with log_path.open("w", encoding="utf-8") as stream:
        for command, process, (stdout, stderr) in zip(commands, processes,
                                                      outputs):
            stream.write("$ " + " ".join(command) + "\n")
            stream.write("exit=" + str(process.returncode) + "\n")
            stream.write("--- stdout ---\n" + stdout)
            stream.write("--- stderr ---\n" + stderr)
            if process.returncode != 0:
                raise RuntimeError("parallel client failed:\n" + stderr)
            summaries.append(parse_summary(stdout))

    row = {
        "experiment": "concurrency",
        "case": f"clients={clients}",
        "run": run,
        "method": "GET",
        "path": "/medium",
        "size": 8192,
        "clients": clients,
        "count": sum(int(item["requests"]) for item in summaries),
        "total_ms": f"{wall_ms:.3f}",
        "requests": sum(int(item["requests"]) for item in summaries),
        "success": sum(int(item["success"]) for item in summaries),
        "errors": sum(int(item["errors"]) for item in summaries),
        "timeouts": sum(int(item["timeouts"]) for item in summaries),
        "status_2xx": sum(int(item["status_2xx"]) for item in summaries),
        "status_4xx": sum(int(item["status_4xx"]) for item in summaries),
        "sent_bytes": sum(int(item["sent_bytes"]) for item in summaries),
        "recv_bytes": sum(int(item["recv_bytes"]) for item in summaries),
        "avg_request_ms": "",
        "avg_connect_ms": "",
        "avg_ttfb_ms": "",
        "throughput_kib_s": (
            sum(int(item["recv_bytes"]) for item in summaries) / 1024.0 /
            (wall_ms / 1000.0)
        ),
        "exit_code": 0,
    }
    row["throughput_kib_s"] = f"{row['throughput_kib_s']:.3f}"
    return row


def concurrency_experiment(rows):
    for clients in (1, 2, 4, 8):
        for run in range(1, REPEATS + 1):
            name = f"concurrency_{clients}_run{run}"
            rows.append(parallel_run(name, clients, 50, run))


def error_experiment(rows):
    for run in range(1, REPEATS + 1):
        summary, code = run_client(
            f"error_timeout_run{run}", "/slow", count=10, timeout_ms=10,
            allow_failure=True)
        row = base_row("errors", "timeout", run, summary, code)
        rows.append(row)

        summary, code = run_client(
            f"error_unavailable_run{run}", "/", count=10, timeout_ms=500,
            port="1", allow_failure=True)
        row = base_row("errors", "unavailable", run, summary, code)
        rows.append(row)

        summary, code = run_client(
            f"error_chunked_run{run}", "/chunked", count=10,
            allow_failure=True)
        row = base_row("errors", "chunked", run, summary, code)
        rows.append(row)


def main():
    if not CLIENT.is_file() or not SERVER.is_file():
        raise SystemExit("build client and server first: cmake -S LR5 -B LR5/build && cmake --build LR5/build")
    if REPORT_DATA.exists():
        shutil.rmtree(REPORT_DATA)
    RAW.mkdir(parents=True)
    rows = []
    server = start_server()
    try:
        size_experiment(rows)
        method_experiment(rows)
        concurrency_experiment(rows)
        error_experiment(rows)
    finally:
        stop_server(server)

    with RESULTS.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {RESULTS}")


if __name__ == "__main__":
    main()
