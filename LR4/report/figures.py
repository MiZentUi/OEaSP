#!/usr/bin/env python3
"""Build SVG figures for the LR4 report from measured CSV data."""
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


def groups(rows, experiment):
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
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="#FFFFFF"/>\n'
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


def bar_chart(path, categories, series, y_label, x_label, y_digits=0,
              y_unit="", label_bars=False):
    width, height = 780, 430
    left, right, top, bottom = 100, 35, 35, 95
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

    for tick in range(5):
        value = tick * y_max / 4
        y = top + plot_h - (value / y_max) * plot_h
        svg += line(left, y, left + plot_w, y, LIGHT, 1.0)
        svg += text(left - 14, y + 5, fmt(value, y_digits) + y_unit,
                    13, GRAY, "end")

    svg += line(left, top, left, top + plot_h, "#202124", 1.6)
    svg += line(left, top + plot_h, left + plot_w, top + plot_h, "#202124", 1.6)
    svg += text(left + plot_w / 2, height - 28, x_label, 15, "#202124")
    svg += text(24, top + plot_h / 2, y_label, 15, "#202124",
                "middle", extra=f'transform="rotate(-90 24 {top + plot_h / 2:.1f})"')

    group_w = plot_w / len(categories)
    group_inner = group_w * 0.66
    bar_w = group_inner / len(series)
    for index, category in enumerate(categories):
        center = left + group_w * (index + 0.5)
        svg += text(center, top + plot_h + 28, category, 14, "#202124")
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
            if label_bars:
                svg += text(x, y - 8, fmt(avg, y_digits) + y_unit, 12,
                            item["color"], "middle", "bold")

    legend_x = left
    legend_y = 18
    for item in series:
        svg += (
            f'<rect x="{legend_x}" y="{legend_y - 11}" width="18" height="12" '
            f'fill="{item["color"]}" fill-opacity="0.82"/>\n'
        )
        svg += text(legend_x + 25, legend_y, item["name"], 14, "#202124", "start")
        legend_x += 25 + len(item["name"]) * 8 + 30

    svg += "</svg>\n"
    path.write_text(svg, encoding="utf-8")


def interaction_svg(path):
    width, height = 1000, 610
    svg = svg_header(width, height)
    svg += (
        '<defs>\n'
        '<marker id="arrow-gray" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="9" markerHeight="9" orient="auto">'
        '<path d="M 0 0 L 10 5 L 0 10 z" fill="#5F6368"/></marker>\n'
        '<marker id="arrow-blue" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="9" markerHeight="9" orient="auto">'
        '<path d="M 0 0 L 10 5 L 0 10 z" fill="#1A73E8"/></marker>\n'
        '<marker id="arrow-green" viewBox="0 0 10 10" refX="9" refY="5" '
        'markerWidth="9" markerHeight="9" orient="auto">'
        '<path d="M 0 0 L 10 5 L 0 10 z" fill="#137333"/></marker>\n'
        '</defs>\n'
    )

    def box(x, y, w, h, fill, stroke, dash=None):
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        return (
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="2"{dash_attr}/>\n'
        )

    def arrow(path_data, color="#5F6368", marker="gray", dash=None):
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        return (
            f'<path d="{path_data}" fill="none" stroke="{color}" '
            f'stroke-width="2.2" stroke-linecap="round" '
            f'stroke-linejoin="round" marker-end="url(#arrow-{marker})"{dash_attr}/>\n'
        )

    # Manager
    svg += box(330, 24, 340, 92, "#E8F0FE", BLUE)
    svg += text(500, 56, "MANAGER", 25, "#202124", weight="bold")
    svg += text(500, 82, "shm_open · fork ×4 · start_gate", 15, "#3C4043")
    svg += text(500, 103, "waitpid · verify_final · shm_unlink", 14, GRAY)

    # Shared memory
    svg += box(310, 230, 380, 188, "#E6F4EA", GREEN)
    svg += text(500, 262, "СОВМЕСТНАЯ ПАМЯТЬ", 20, "#202124", weight="bold")
    svg += text(500, 288, "shared_state_t", 16, GREEN)
    svg += text(500, 312, "record_t table[100]  ·  generation", 14, "#3C4043")
    svg += text(500, 334, "readers_mutex  ·  journal_mutex", 14, "#3C4043")
    svg += text(500, 356, "turnstile  ·  access_gate  ·  start_gate", 14, "#3C4043")
    svg += text(500, 378, "active_readers  ·  active_writers  ·  stats[4]", 14, "#3C4043")
    svg += text(500, 400, "PTHREAD_PROCESS_SHARED / pshared = 1", 12, GRAY)

    # Writers
    for index, y in enumerate((180, 330)):
        svg += box(35, y, 220, 92, "#F1F3F4", GRAY)
        svg += text(145, y + 31, f"WRITER #{index}", 20, "#202124", weight="bold")
        svg += text(145, y + 55, "rwlock_writer_enter()", 13, "#3C4043")
        svg += text(145, y + 76, "model_write() → leave()", 13, "#3C4043")

    # Readers
    for index, y in enumerate((180, 330)):
        svg += box(745, y, 220, 92, "#F1F3F4", GRAY)
        svg += text(855, y + 31, f"READER #{index}", 20, "#202124", weight="bold")
        svg += text(855, y + 55, "rwlock_reader_enter()", 13, "#3C4043")
        svg += text(855, y + 76, "model_check() → leave()", 13, "#3C4043")

    # Manager forks
    svg += arrow("M 330 70 H 18 V 226 H 33", "#5F6368", "gray")
    svg += arrow("M 330 70 H 18 V 376 H 33", "#5F6368", "gray")
    svg += arrow("M 670 70 H 982 V 226 H 967", "#5F6368", "gray")
    svg += arrow("M 670 70 H 982 V 376 H 967", "#5F6368", "gray")
    svg += text(28, 160, "fork()", 15, GRAY, "start")
    svg += text(972, 160, "fork()", 15, GRAY, "end")

    # Manager initializes and cleans shared memory
    svg += arrow("M 500 116 V 226", "#137333", "green")
    svg += text(512, 168, "shm_create / init / verify / unlink", 13, GREEN, "start")

    # Writers and readers access shared memory
    svg += arrow("M 255 226 H 306", "#1A73E8", "blue")
    svg += arrow("M 255 376 H 306", "#1A73E8", "blue")
    svg += text(280, 214, "write", 13, BLUE)
    svg += arrow("M 745 226 H 694", "#1A73E8", "blue")
    svg += arrow("M 745 376 H 694", "#1A73E8", "blue")
    svg += text(719, 214, "read", 13, BLUE)

    # Start gate and completion notes
    svg += text(500, 470, "Старт: менеджер инициализирует состояние и разрешает работу через start_gate",
                15, "#202124")
    svg += text(500, 498, "Завершение: менеджер ждёт процессы, проверяет инварианты и удаляет объект shm",
                15, "#202124")

    # Legend
    legend_y = 555
    svg += line(70, legend_y, 115, legend_y, GRAY, 2.2)
    svg += text(124, legend_y + 5, "fork()", 14, "#3C4043", "start")
    svg += line(255, legend_y, 300, legend_y, BLUE, 2.2)
    svg += text(309, legend_y + 5, "доступ к данным", 14, "#3C4043", "start")
    svg += line(475, legend_y, 520, legend_y, GREEN, 2.2)
    svg += text(529, legend_y + 5, "жизненный цикл shm", 14, "#3C4043", "start")

    svg += "</svg>\n"
    path.write_text(svg, encoding="utf-8")


def main():
    rows = load_rows()
    read_groups = groups(rows, "read_duration")
    read_order = ["read_us=0", "read_us=100", "read_us=1000", "read_us=5000"]
    categories = ["0", "100", "1 000", "5 000"]

    throughput_values = [
        [float(row["throughput_ops_s"]) for row in read_groups[case]]
        for case in read_order
    ]
    bar_chart(
        CHARTS / "throughput.svg",
        categories,
        [{"name": "среднее по 3 запускам", "color": BLUE, "values": throughput_values}],
        "Пропускная способность, операций/с",
        "Длительность чтения, мкс",
        label_bars=False,
    )

    writer_values = [
        [float(row["avg_writer_wait_ms"]) for row in read_groups[case]]
        for case in read_order
    ]
    reader_values = [
        [float(row["avg_reader_wait_ms"]) for row in read_groups[case]]
        for case in read_order
    ]
    bar_chart(
        CHARTS / "wait.svg",
        categories,
        [
            {"name": "Writer", "color": RED, "values": writer_values},
            {"name": "Reader", "color": BLUE, "values": reader_values},
        ],
        "Среднее ожидание, мс",
        "Длительность чтения, мкс",
        y_digits=1,
    )

    interaction_svg(ROOT / "interaction.svg")


if __name__ == "__main__":
    main()
