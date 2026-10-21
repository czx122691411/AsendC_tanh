#!/usr/bin/env python3
"""Visualize CPU Tanh references for the learning harness (not kernel results).

No third-party packages are required for SVG/CSV output. --png additionally
requires Pillow. Input conversion follows float32 arithmetic -> IEEE FP16.
"""

import argparse
import csv
import html
import math
from pathlib import Path
import struct


DEFAULT_LENGTHS = (16384, 16400, 16380, 100, 2050, 31, 1)
COVERAGE37 = (
    0.0, 2**-14, -(2**-14), 2**-10, -(2**-10),
    0.015625, -0.015625, 0.0625, -0.0625,
    0.25, -0.25, 0.5, -0.5, 0.75, -0.75,
    1.0, -1.0, 2.0, -2.0, 3.0, -3.0,
    4.0, -4.0, 4.5, -4.5, 4.50390625, -4.50390625,
    4.5078125, -4.5078125,
    5.0, -5.0, 8.0, -8.0, 10.0, -10.0, 125.0, -125.0,
)
BLUE, ORANGE, INK = '#2563eb', '#b45309', '#172033'
MUTED, GRID = '#475569', '#dce3ed'


def fp32(value):
    """Round once to IEEE binary32, matching the float arithmetic in main.cpp."""
    return struct.unpack('<f', struct.pack('<f', value))[0]


def fp16(value):
    """Round to IEEE binary16; returned Python float stores that value exactly."""
    return struct.unpack('<e', struct.pack('<e', value))[0]


def make_input(index, mode):
    if mode == 'sweep':
        # main.cpp: half(float(int32_t(i % 601u) - 300) / 100.0f).
        return fp16(fp32(fp32(index % 601 - 300) / fp32(100.0)))
    if mode == 'coverage37':
        return fp16(COVERAGE37[index % len(COVERAGE37)])
    return fp16(index % 251 - 125)  # Earlier identity-copy integer input.


def generate(length, mode):
    x = [make_input(i, mode) for i in range(length)]
    reference = [math.tanh(value) for value in x]
    rounded = [fp16(value) for value in reference]
    return x, reference, rounded


def histogram(values, bins):
    counts = [0] * bins
    for value in values:
        # [-1,1]; the final bin includes +1. No data are dropped.
        position = min(bins - 1, max(0, int((value + 1.0) * bins / 2.0)))
        counts[position] += 1
    if sum(counts) != len(values):
        raise RuntimeError('Histogram lost observations')
    return counts


class Drawing:
    """Record simple marks once, then export both vector and optional bitmap."""

    def __init__(self, width, height):
        self.width, self.height = width, height
        self.marks = []

    def text(self, x, y, value, size=16, fill=INK, anchor='start'):
        self.marks.append(('text', (x, y, str(value), size, fill, anchor)))

    def line(self, x1, y1, x2, y2, fill=GRID, width=1):
        self.marks.append(('line', (x1, y1, x2, y2, fill, width)))

    def rect(self, x, y, width, height, fill):
        self.marks.append(('rect', (x, y, width, height, fill)))

    def polyline(self, points, fill=BLUE, width=1.6):
        self.marks.append(('polyline', (points, fill, width)))

    def circle(self, x, y, radius=3, fill=BLUE):
        self.marks.append(('circle', (x, y, radius, fill)))

    def save_svg(self, path):
        out = [
            '<svg xmlns="http://www.w3.org/2000/svg" '
            'width="%d" height="%d" viewBox="0 0 %d %d" '
            'role="img" aria-label="Tanh CPU reference distributions">'
            % (self.width, self.height, self.width, self.height),
            '<rect width="100%" height="100%" fill="#ffffff"/>',
            '<g font-family="DejaVu Sans,Arial,sans-serif">',
        ]
        for kind, args in self.marks:
            if kind == 'text':
                x, y, value, size, fill, anchor = args
                out.append('<text x="%g" y="%g" font-size="%g" fill="%s" '
                           'text-anchor="%s">%s</text>'
                           % (x, y, size, fill, anchor, html.escape(value)))
            elif kind == 'line':
                x1, y1, x2, y2, fill, width = args
                out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" '
                           'stroke="%s" stroke-width="%g"/>'
                           % (x1, y1, x2, y2, fill, width))
            elif kind == 'rect':
                x, y, w, h, fill = args
                out.append('<rect x="%g" y="%g" width="%g" height="%g" '
                           'fill="%s"/>' % (x, y, w, h, fill))
            elif kind == 'polyline':
                points, fill, width = args
                coords = ' '.join('%g,%g' % p for p in points)
                out.append('<polyline points="%s" fill="none" stroke="%s" '
                           'stroke-width="%g"/>' % (coords, fill, width))
            elif kind == 'circle':
                x, y, radius, fill = args
                out.append('<circle cx="%g" cy="%g" r="%g" fill="%s"/>'
                           % (x, y, radius, fill))
        out.append('</g></svg>')
        path.write_text('\n'.join(out), encoding='utf-8')

    def save_png(self, path):
        try:
            from PIL import Image, ImageDraw, ImageFont
        except ImportError as exc:
            raise RuntimeError('--png requires Pillow: python3 -m pip install pillow') from exc
        image = Image.new('RGB', (self.width, self.height), 'white')
        painter = ImageDraw.Draw(image)
        fonts = {}

        def font(size):
            if size not in fonts:
                candidates = (
                    'DejaVuSans.ttf',
                    '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
                    'C:/Windows/Fonts/arial.ttf',
                )
                for candidate in candidates:
                    try:
                        fonts[size] = ImageFont.truetype(candidate, size)
                        break
                    except OSError:
                        pass
                else:
                    fonts[size] = ImageFont.load_default()
            return fonts[size]

        for kind, args in self.marks:
            if kind == 'text':
                x, y, value, size, fill, anchor = args
                face = font(size)
                bounds = painter.textbbox((0, 0), value, font=face)
                w = bounds[2] - bounds[0]
                if anchor == 'middle':
                    x -= w / 2
                elif anchor == 'end':
                    x -= w
                ascent = face.getmetrics()[0] if hasattr(face, 'getmetrics') else size
                painter.text((x, y - ascent), value, font=face, fill=fill)
            elif kind == 'line':
                x1, y1, x2, y2, fill, width = args
                painter.line((x1, y1, x2, y2), fill=fill, width=max(1, round(width)))
            elif kind == 'rect':
                x, y, w, h, fill = args
                if w > 0 and h > 0:
                    painter.rectangle((x, y, x + w, y + h), fill=fill)
            elif kind == 'polyline':
                points, fill, width = args
                if len(points) >= 2:
                    painter.line(points, fill=fill, width=max(1, round(width)))
            elif kind == 'circle':
                x, y, r, fill = args
                painter.ellipse((x-r, y-r, x+r, y+r), fill=fill)
        image.save(path)


def draw_axes(drawing, left, top, width, height, kind, last_index=1):
    drawing.rect(left, top, width, height, '#f8fafc')
    if kind == 'histogram':
        yticks, xticks = (0, 25, 50, 75, 100), (-1, -0.5, 0, 0.5, 1)
    else:
        yticks = (-1, -0.5, 0, 0.5, 1)
        xticks = sorted(set((0, last_index // 2, last_index)))
    for value in yticks:
        y = top + height * (1 - value / 100) if kind == 'histogram' else top + height * (1 - value) / 2
        drawing.line(left, y, left + width, y)
        drawing.text(left - 10, y + 5, '%g' % value, 13, MUTED, 'end')
    for value in xticks:
        fraction = (value + 1) / 2 if kind == 'histogram' else value / max(1, last_index)
        x = left + width * fraction
        drawing.line(x, top + height, x, top + height + 5)
        drawing.text(x, top + height + 23, '%g' % value, 13, MUTED, 'middle')
    drawing.line(left, top, left, top + height, MUTED)
    drawing.line(left, top + height, left + width, top + height, MUTED)
    drawing.text(left, top - 10, 'Elements per bin (%)' if kind == 'histogram' else 'FP16 reference y', 13, MUTED)
    drawing.text(left + width / 2, top + height + 45,
                 'FP16-rounded Tanh reference y' if kind == 'histogram' else 'Element index i',
                 14, MUTED, 'middle')


def make_figure(datasets, bins, mode, sequence=False, sequence_limit=1202):
    rows = (len(datasets) + 1) // 2
    width, height = 1480, 145 + 320 * rows + 40
    drawing = Drawing(width, height)
    title = 'Tanh output sequence' if sequence else 'Tanh output distributions'
    drawing.text(36, 38, title + ' | CPU reference only', 27)
    labels = {
        'sweep': 'Input: half(float(i % 601 - 300) / 100.0f); period = 601 elements',
        'coverage37': 'Input: 37 fixed FP16 points near zero, in transition, near saturation and saturated',
        'integer': 'Input: half(i % 251 - 125); period = 251 elements',
    }
    drawing.text(36, 68, labels[mode], 17, MUTED)
    drawing.text(36, 96, 'y_ref = math.tanh(actual FP16 input); plotted y = FP16(y_ref). These are not kernel measurements.', 15, MUTED)
    drawing.text(36, 120, 'Shared axes; histogram percentages use all valid elements. No guard or padding elements included.', 15, MUTED)

    for panel, (length, x, reference, rounded) in enumerate(datasets):
        col, row = panel % 2, panel // 2
        origin_x, origin_y = 34 + col * 730, 150 + row * 320
        saturated = sum(abs(value) == 1 for value in rounded)
        drawing.text(origin_x, origin_y + 5, 'N = %d' % length, 21)
        drawing.text(origin_x + 175, origin_y + 5,
                     'x: [%.6g, %.6g] | exact +/-1: %d/%d (%.2f%%)'
                     % (min(x), max(x), saturated, length, 100 * saturated / length), 14, MUTED)
        left, top, plot_w, plot_h = origin_x + 52, origin_y + 49, 610, 160
        if sequence:
            shown = rounded[:sequence_limit]
            last_index = max(0, len(shown) - 1)
            draw_axes(drawing, left, top, plot_w, plot_h, 'sequence', last_index)
            points = [(left + plot_w * i / max(1, last_index), top + plot_h * (1-v) / 2)
                      for i, v in enumerate(shown)]
            if len(points) == 1:
                drawing.circle(*points[0], radius=4)
            else:
                drawing.polyline(points)
            caption = 'Showing first %d/%d elements; y range over all N: [%.7g, %.7g]'
            drawing.text(origin_x + 52, origin_y + 282,
                         caption % (len(shown), length, min(rounded), max(rounded)), 13, MUTED)
        else:
            draw_axes(drawing, left, top, plot_w, plot_h, 'histogram')
            for index, count in enumerate(histogram(rounded, bins)):
                bar_h = plot_h * count / length
                drawing.rect(left + plot_w * index / bins,
                             top + plot_h - bar_h,
                             plot_w / bins * 0.9, bar_h, BLUE)
            drawing.text(origin_x + 52, origin_y + 282,
                         'y range: [%.7g, %.7g] | %d bins across [-1,1]'
                         % (min(rounded), max(rounded), bins), 13, MUTED)

    drawing.text(36, height - 18,
                 'Small N uses only the beginning of the same input sequence; a concentrated distribution is not a kernel failure.',
                 15, ORANGE)
    return drawing


def write_summary(path, datasets):
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.writer(stream)
        writer.writerow(('total_length', 'input_min', 'input_max', 'reference_min', 'reference_max',
                         'fp16_output_min', 'fp16_output_max', 'negative_count', 'zero_count',
                         'positive_count', 'exact_minus_one', 'exact_plus_one', 'saturated_fraction'))
        for n, x, ref, rounded in datasets:
            minus = rounded.count(-1.0)
            plus = rounded.count(1.0)
            writer.writerow((n, min(x), max(x), min(ref), max(ref), min(rounded), max(rounded),
                             sum(v < 0 for v in rounded), rounded.count(0.0),
                             sum(v > 0 for v in rounded), minus, plus, (minus + plus) / n))


def positive_integer(text):
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError('must be greater than zero')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lengths', nargs='+', type=positive_integer, default=DEFAULT_LENGTHS)
    parser.add_argument('--input-mode', choices=('sweep', 'coverage37', 'integer'), default='sweep')
    parser.add_argument('--bins', type=positive_integer, default=40)
    parser.add_argument('--sequence-limit', type=positive_integer, default=1202)
    parser.add_argument('--output-dir', type=Path, default=Path(__file__).resolve().parent / 'plots')
    parser.add_argument('--export-csv', action='store_true', help='Also export every input and output reference')
    parser.add_argument('--png', action='store_true', help='Also produce PNG previews (requires Pillow)')
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    datasets = [(n, *generate(n, args.input_mode)) for n in args.lengths]

    print('CPU numerical reference only; no Ascend C kernel or NPU was executed.')
    print('input_mode=%s; histogram_bins=%d' % (args.input_mode, args.bins))
    print('%7s  %24s  %24s  %12s' % ('N', 'input range', 'FP16 output range', 'exact +/-1'))
    for n, x, reference, rounded in datasets:
        saturated = sum(abs(v) == 1 for v in rounded)
        print('%7d  [%10.6g,%10.6g]  [%10.7g,%10.7g]  %11.2f%%'
              % (n, min(x), max(x), min(rounded), max(rounded), 100 * saturated / n))
        if args.export_csv:
            with (args.output_dir / ('tanh_%d.csv' % n)).open('w', newline='', encoding='utf-8') as stream:
                writer = csv.writer(stream)
                writer.writerow(('index', 'x_fp16', 'reference_double', 'reference_rounded_fp16'))
                writer.writerows((i, a, b, c) for i, (a, b, c) in enumerate(zip(x, reference, rounded)))

    write_summary(args.output_dir / 'summary.csv', datasets)
    for sequence, name in ((False, 'tanh_distribution'), (True, 'tanh_sequence')):
        figure = make_figure(datasets, args.bins, args.input_mode, sequence, args.sequence_limit)
        path = args.output_dir / (name + '.svg')
        figure.save_svg(path)
        print('Saved:', path.resolve())
        if args.png:
            try:
                figure.save_png(args.output_dir / (name + '.png'))
            except RuntimeError as exc:
                parser.exit(2, str(exc) + '\nSVG files remain usable without Pillow.\n')
    print('Saved:', (args.output_dir / 'summary.csv').resolve())


if __name__ == '__main__':
    main()
