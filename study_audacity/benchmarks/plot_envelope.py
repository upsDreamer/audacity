# Envelope 数据导出与可视化（用户裁决 2026-10-20：Envelope 拆不开 → Python 绘图呈现）
#
# 复刻 au3-mixer/Envelope::GetValues 的插值语义（非真实代码，仅数据呈现）：
#   - 线性域（mDB=false）：控制点间分段线性，段内增量法每样本 1 FMA
#   - dB 域（mDB=true）：控制点在 log10 域插值，每样本两次 pow(10,·)
#     （真实代码 Envelope.cpp:1120-1129 的成本来源）
#
# 用法：
#   python plot_envelope.py                # 内置演示包络
#   python plot_envelope.py points.csv     # 自定义控制点 CSV（每行 "time,gain"）
#
# 输出：
#   docs/img/envelope_interpolation.png    # 曲线对比图
#   benchmarks/output/envelope_values.csv  # 求值数据导出（time_s,linear_db_domain）
import csv
import math
import os
import sys

try:
    import matplotlib

    matplotlib.use("Agg")
    # 中文标签需要 CJK 字体（Windows 自带微软雅黑）
    matplotlib.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
    matplotlib.rcParams["axes.unicode_minus"] = False
    import matplotlib.pyplot as plt
except ImportError:
    print("需要 matplotlib：pip install matplotlib")
    sys.exit(1)

HERE = os.path.dirname(os.path.abspath(__file__))
IMG_DIR = os.path.normpath(os.path.join(HERE, "..", "docs", "img"))
OUT_DIR = os.path.join(HERE, "output")


def load_points(path):
    points = []
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.reader(f):
            if not row or row[0].lstrip().startswith("#"):
                continue
            points.append((float(row[0]), float(row[1])))
    return points


def get_values(points, t0, tstep, n, db_domain):
    """复刻 Envelope::GetValues：返回 n 个增益值。

    线性域：段内 v[i] = v[i-1] + vstep（增量法，等价真实代码 1126-1131）。
    dB 域：段内对插值量做 pow(10, v/10) 两次换算（等价 1120-1129 的双 pow）。
    """
    values = []
    for i in range(n):
        t = t0 + tstep * i
        # 找段（真实代码用带上次位置缓存的二分；演示规模用线性扫）
        if t <= points[0][0]:
            v = points[0][1]
        elif t >= points[-1][0]:
            v = points[-1][1]
        else:
            for k in range(len(points) - 1):
                t1, v1 = points[k]
                t2, v2 = points[k + 1]
                if t1 <= t <= t2:
                    if t2 == t1:
                        v = v2
                    else:
                        u = (t - t1) / (t2 - t1)
                        if db_domain:
                            # 真实代码：插值在对数域进行，样本处两次 pow(10, x/10)
                            lo = 10.0 ** (v1 / 10.0)
                            hi = 10.0 ** (v2 / 10.0)
                            v = 10.0 * math.log10(lo + (hi - lo) * u)
                        else:
                            v = v1 + (v2 - v1) * u
                    break
        values.append(v)
    return values


DEMO = {
    "淡入（4 控制点）": [(0.0, 0.0), (1.0, 0.4), (2.5, 0.85), (4.0, 1.0)],
    "自动化深谷": [(0.0, 1.0), (1.0, 1.0), (1.3, 0.1), (1.8, 0.1), (2.1, 1.0), (4.0, 1.0)],
    "衰减到 -60dB（dB 域意义明显）": [
        (0.0, 1.0), (1.0, 0.5), (2.0, 0.1), (3.0, 0.01), (4.0, 0.001)],
}


def main():
    os.makedirs(IMG_DIR, exist_ok=True)
    os.makedirs(OUT_DIR, exist_ok=True)

    if len(sys.argv) > 1:
        shapes = {"自定义控制点": load_points(sys.argv[1])}
    else:
        shapes = DEMO

    RATE = 44100.0
    DUR = 4.0
    N = int(RATE * DUR)

    fig, axes = plt.subplots(len(shapes), 2, figsize=(12, 3.2 * len(shapes)), squeeze=False)
    csv_rows = []

    for r, (name, points) in enumerate(shapes.items()):
        linear = get_values(points, 0.0, 1.0 / RATE, N, db_domain=False)
        dbdom = get_values(points, 0.0, 1.0 / RATE, N, db_domain=True)
        t = [i / RATE for i in range(N)]

        ax = axes[r][0]
        ax.plot(t, linear, lw=1.0, label="线性域插值（1 FMA/样本）")
        cp_t = [p[0] for p in points]
        cp_v = [p[1] for p in points]
        ax.plot(cp_t, cp_v, "o", ms=5, color="tab:red", label="控制点")
        ax.set_title(f"{name} — 求值结果")
        ax.set_xlabel("时间 (s)")
        ax.set_ylabel("增益")
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8)

        ax2 = axes[r][1]
        diff = [abs(a - b) for a, b in zip(linear, dbdom)]
        ax2.plot(t, diff, lw=0.8, color="tab:purple")
        ax2.set_title(f"{name} — 两种插值域的差异（dB 域每样本 2 次 pow(10,·)）")
        ax2.set_xlabel("时间 (s)")
        ax2.set_ylabel("|线性 − dB|")
        ax2.grid(alpha=0.3)

        for i in range(0, N, max(1, N // 800)):  # 稀疏导出
            csv_rows.append((name, t[i], linear[i], dbdom[i]))

    fig.tight_layout()
    out_png = os.path.join(IMG_DIR, "envelope_interpolation.png")
    fig.savefig(out_png, dpi=130)
    print(f"图：{out_png}")

    out_csv = os.path.join(OUT_DIR, "envelope_values.csv")
    with open(out_csv, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["shape", "time_s", "gain_linear_domain", "gain_db_domain"])
        w.writerows(csv_rows)
    print(f"数据：{out_csv}（{len(csv_rows)} 行，稀疏导出）")

    # 关键量化：dB 域与线性域的最大差异 + dB 域每样本双 pow 的成本注记
    for name, points in shapes.items():
        linear = get_values(points, 0.0, 1.0 / RATE, N, False)
        dbdom = get_values(points, 0.0, 1.0 / RATE, N, True)
        mx = max(abs(a - b) for a, b in zip(linear, dbdom))
        print(f"  {name}: 域间最大差异 = {mx:.4f}（增益）")


if __name__ == "__main__":
    main()
