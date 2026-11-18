#!/usr/bin/env python3
"""Build SVG charts and the sequence diagram for the LR5 report."""
import csv
import math
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA = ROOT / "data" / "results.csv"
CHARTS = ROOT / "charts"
CHARTS.mkdir(parents=True, exist_ok=True)

FONT = "DejaVu Sans, Arial, sans-serif"
BLUE = "#1A73E8"
RED = "#D93025"
GREEN = "#137333"
GRAY = "#5F6368"
LIGHT = "#E8EAED"


def load_rows():
    with DATA.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def group(rows, experiment):
    result = {}
    for row in rows:
        if row["experiment"] != experiment:
            continue
        result.setdefault(row["case"], []).append(row)
    return result


def mean(values):
    return statistics.mean(values)


def fmt(value, digits=0):
    if digits == 0:
        text = f"{value:,.0f}".replace(",", " ")
    else:
        text = f"{value:,.{digits}f}".replace(",", " ").replace(".", ",")
    return text


def svg_header(width, height):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}" '
        f'font-family="{FONT}">\n'
        f'<rect width="{width}" height="{height}" fill="#FFFFFF"/>\n'
    )


def text(x, y, value, size=15, color="#202124", anchor="middle",
         weight="normal", extra=""):
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" '
        f'font-weight="{weight}" fill="{color}" text-anchor="{anchor}" '
        f'{extra}>{value}</text>\n'
    )


def line(x1, y1, x2, y2, color="#202124", width=1.5, dash=None):
    dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
    return (
        f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
        f'stroke="{color}" stroke-width="{width}"{dash_attr}/>\n'
    )


def bar_chart(path, title, categories, series, y_label, x_label,
              y_digits=0, y_unit=""):
    width, height = 820, 480
    left, right, top, bottom = 105, 35, 90, 100
    plot_w = width - left - right
    plot_h = height - top - bottom

    maximum = 0.0
    for item in series:
        for values in item["values"]:
            maximum = max(maximum, max(values))
    if maximum <= 0:
        maximum = 1.0
    step = maximum / 4.0
    magnitude = 10 ** math.floor(math.log10(step)) if step > 0 else 1
    for multiplier in (1, 2, 2.5, 5, 10):
        candidate = multiplier * magnitude
        if candidate >= step:
            step = candidate
            break
    y_max = step * 4
    if y_max < maximum:
        y_max += step

    svg = svg_header(width, height)
    svg += text(width / 2, 24, title, 17, "#202124", weight="bold")

    for tick in range(5):
        value = tick * y_max / 4
        y = top + plot_h - (value / y_max) * plot_h
        svg += line(left, y, left + plot_w, y, LIGHT, 1.0)
        svg += text(left - 14, y + 5, fmt(value, y_digits) + y_unit,
                    13, GRAY, "end")

    svg += line(left, top, left, top + plot_h, "#202124", 1.6)
    svg += line(left, top + plot_h, left + plot_w, top + plot_h,
                "#202124", 1.6)
    svg += text(left + plot_w / 2, height - 26, x_label, 15)
    svg += text(24, top + plot_h / 2, y_label, 15, "#202124", "middle",
                extra=f'transform="rotate(-90 24 {top + plot_h / 2:.1f})"')

    group_w = plot_w / len(categories)
    group_inner = group_w * 0.64
    bar_w = group_inner / len(series)
    for index, category in enumerate(categories):
        center = left + group_w * (index + 0.5)
        svg += text(center, top + plot_h + 28, category, 14)
        for series_index, item in enumerate(series):
            values = item["values"][index]
            avg = mean(values)
            low = min(values)
            high = max(values)
            x = center - group_inner / 2 + bar_w * (series_index + 0.5)
            bar_h = (avg / y_max) * plot_h
            y = top + plot_h - bar_h
            svg += (
                f'<rect x="{x - bar_w * 0.36:.1f}" y="{y:.1f}" '
                f'width="{bar_w * 0.72:.1f}" height="{bar_h:.1f}" '
                f'fill="{item["color"]}" fill-opacity="0.82"/>\n'
            )
            low_y = top + plot_h - (low / y_max) * plot_h
            high_y = top + plot_h - (high / y_max) * plot_h
            svg += line(x, low_y, x, high_y, GRAY, 1.2)
            svg += line(x - 5, low_y, x + 5, low_y, GRAY, 1.2)
            svg += line(x - 5, high_y, x + 5, high_y, GRAY, 1.2)
            svg += text(x, y - 8, fmt(avg, y_digits), 12, item["color"],
                        "middle", "bold")

    legend_x = left
    for item in series:
        svg += (
            f'<rect x="{legend_x}" y="53" width="18" height="12" '
            f'fill="{item["color"]}" fill-opacity="0.82"/>\n'
        )
        svg += text(legend_x + 25, 64, item["name"], 14, "#202124", "start")
        legend_x += 25 + len(item["name"]) * 8 + 30

    svg += "</svg>\n"
    path.write_text(svg, encoding="utf-8")


def interaction_svg(path):
    width, height = 1200, 900
    svg = svg_header(width, height)
    svg += (
        '<defs>\n'
        '<marker id="arrow-gray" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="9" markerHeight="9" orient="auto">'
        '<path d="M 0 0 L 10 5 L 0 10 z" fill="#5F6368"/></marker>\n'
        '<marker id="arrow-blue" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="9" markerHeight="9" orient="auto">'
        '<path d="M 0 0 L 10 5 L 0 10 z" fill="#1A73E8"/></marker>\n'
        '<marker id="arrow-red" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="9" markerHeight="9" orient="auto">'
        '<path d="M 0 0 L 10 5 L 0 10 z" fill="#D93025"/></marker>\n'
        '</defs>\n'
    )
    lanes = {"client1": 170, "client2": 500, "server": 830, "resource": 1120}

    def arrow(start, end, y, label, color=BLUE, marker="blue", dash=None):
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        start_x = lanes[start]
        end_x = lanes[end]
        result = (
            f'<path d="M {start_x} {y} H {end_x}" fill="none" '
            f'stroke="{color}" stroke-width="2" '
            f'marker-end="url(#arrow-{marker})"{dash_attr}/>\n'
        )
        result += text((start_x + end_x) / 2, y - 10, label, 18, color,
                       extra='paint-order="stroke" stroke="#FFFFFF" '
                             'stroke-width="5" stroke-linejoin="round"')
        return result

    def note(x, y, value, color=GRAY, size=17):
        return text(x, y, value, size, color)

    svg += text(width / 2, 28,
                "Последовательность HTTP-взаимодействия", 24,
                "#202124", weight="bold")

    for name, x in lanes.items():
        labels = {
            "client1": "CLIENT 1", "client2": "CLIENT 2",
            "server": "SERVER", "resource": "TEST RESOURCE",
        }
        svg += (
            f'<rect x="{x - 100}" y="50" width="200" height="42" rx="8" '
            f'fill="#F1F3F4" stroke="#5F6368" stroke-width="1.5"/>\n'
        )
        svg += text(x, 78, labels[name], 19, "#202124", weight="bold")
        svg += line(x, 92, x, 850, GRAY, 1.2, "6,5")

    svg += note(lanes["server"], 125,
                "socket, bind, listen; accept в цикле", BLUE)
    svg += arrow("client1", "server", 160, "TCP connect")
    svg += arrow("server", "client1", 195, "TCP established", GREEN, "gray")
    svg += arrow("client1", "server", 240,
                 "GET /medium; Host; Connection: close")
    svg += arrow("server", "resource", 275, "read test resource", GRAY, "gray")
    svg += arrow("resource", "server", 310, "8192 bytes", GRAY, "gray")
    svg += arrow("server", "client1", 350, "200 OK, Content-Length, body")

    svg += arrow("client2", "server", 410, "TCP connect")
    svg += arrow("client2", "server", 450, "HEAD /large")
    svg += arrow("server", "client2", 490,
                 "200 OK, Content-Length: 131072")

    svg += arrow("client1", "server", 555, "GET /chunked")
    svg += arrow("server", "client1", 595,
                 "Transfer-Encoding: chunked")
    svg += note(lanes["client1"] + 130, 630,
                "ERROR: unsupported encoding", RED)
    svg += arrow("client1", "server", 690, "close", RED, "red", "5,4")
    svg += arrow("client2", "server", 730, "close", RED, "red", "5,4")
    svg += note(lanes["server"], 795, "fork-потомок завершён")
    svg += "</svg>\n"
    path.write_text(svg, encoding="utf-8")


def main():
    rows = load_rows()
    size_rows = group(rows, "size")
    size_order = ["empty", "missing", "small", "medium", "large"]
    size_labels = ["0", "14", "128", "8 192", "131 072"]
    size_values = [
        [float(row["avg_request_ms"]) for row in size_rows[case]]
        for case in size_order
    ]

    method_rows = group(rows, "method")
    method_values = [
        [float(row["total_ms"]) for row in method_rows[method]]
        for method in ("GET", "HEAD")
    ]

    concurrency_rows = group(rows, "concurrency")
    client_order = [1, 2, 4, 8]
    concurrency_values = [
        [float(row["throughput_kib_s"])
         for row in concurrency_rows[f"clients={clients}"]]
        for clients in client_order
    ]

    bar_chart(
        CHARTS / "size_time.svg",
        "Среднее время одного запроса к ресурсам разного размера",
        size_labels,
        [{"name": "GET, среднее по 3 запускам", "color": BLUE,
          "values": size_values}],
        "Время запроса, мс",
        "Размер тела ответа, байт",
        y_digits=2,
    )
    bar_chart(
        CHARTS / "methods.svg",
        "GET и HEAD для ресурса /large",
        ["GET", "HEAD"],
        [{"name": "суммарное время 100 запросов", "color": GREEN,
          "values": method_values}],
        "Время, мс",
        "Метод",
        y_digits=1,
    )
    bar_chart(
        CHARTS / "concurrency.svg",
        "Пропускная способность при разном числе клиентов",
        [str(value) for value in client_order],
        [{"name": "агрегированная пропускная способность", "color": RED,
          "values": concurrency_values}],
        "Пропускная способность, КиБ/с",
        "Число клиентов",
    )
    interaction_svg(ROOT / "interaction.svg")


if __name__ == "__main__":
    main()
