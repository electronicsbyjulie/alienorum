#!/usr/bin/env python3
"""Plot retained population audit reports; this is a developer utility."""
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("snapshots/population_refinement")
    files = sorted(folder.glob("[0-9][0-9]_*.json"))
    if not files:
        raise SystemExit("No population audit reports found.")
    reports = [json.loads(path.read_text()) for path in files]
    reference = reports[-1]["solar"]
    final = reports[-1]["generated"]
    figure, axes = plt.subplots(2, 2, figsize=(14, 9), constrained_layout=True)
    stages = np.arange(len(reports))
    for radius, color in [(10, "#0072B2"), (25, "#D55E00")]:
        values = [report["generated"]["counts"].get(str(radius), {}).get("all", 0) for report in reports]
        axes[0, 0].plot(stages, values, "o-", color=color, label=f"Generated, within {radius} ly")
        axes[0, 0].axhline(reference["counts"][str(radius)]["all"], color=color, linestyle="--", alpha=0.7)
    axes[0, 0].set(yscale="log", title="Central counts (dashed: Solar catalog)", ylabel="Individual stars", xlabel="Retained trial")
    axes[0, 0].set_xticks(stages, [path.stem[:2] for path in files])
    axes[0, 0].legend(fontsize=9)

    magnitude_bins = list(range(1, 9))
    positions = np.arange(len(magnitude_bins))
    for offset, report, label, color in [(-0.18, reference, "Solar", "#0072B2"), (0.18, final, "Final generated", "#D55E00")]:
        axes[0, 1].bar(positions+offset, [report["magnitude_bins"].get(str(key), 0) for key in magnitude_bins], 0.36, label=label, color=color)
    axes[0, 1].set(yscale="symlog", title="Apparent V magnitude from center", ylabel="Individual stars")
    axes[0, 1].set_xticks(positions, ["−1–0", "0–1", "1–2", "2–3", "3–4", "4–5", "5–6", "6–6.5"])
    axes[0, 1].legend()

    positions = np.arange(6)
    for offset, report, label, color in [(-0.18, reference, "Solar", "#0072B2"), (0.18, final, "Final generated", "#D55E00")]:
        values = [report["visible_color_bins"].get(str(key), 0)/report["naked_eye"]*100 for key in range(6)]
        axes[1, 0].bar(positions+offset, values, 0.36, label=label, color=color)
    axes[1, 0].set(title="Visible color distribution (V ≤ 6.5)", ylabel="Percent of visible stars", xlabel="B−V color")
    axes[1, 0].set_xticks(positions, ["<0", "0–0.3", "0.3–0.6", "0.6–1", "1–1.5", "≥1.5"])
    axes[1, 0].legend()

    nearest = [report["generated"]["nearest_giants"][0]["ly"] if report["generated"]["nearest_giants"] else float("nan") for report in reports]
    axes[1, 1].plot(stages, nearest, "o-", color="#009E73", label="Nearest generated giant")
    axes[1, 1].axhline(reference["nearest_giants"][0]["ly"], color="#0072B2", linestyle="--", label="Nearest physical Solar giant")
    axes[1, 1].set(title="Nearest luminous giant", ylabel="Light years from center", xlabel="Retained trial", ylim=(0, max(nearest)*1.2))
    axes[1, 1].set_xticks(stages, [path.stem[:2] for path in files])
    axes[1, 1].legend(fontsize=9)
    figure.suptitle("Fictional stellar population refinement — fixed comparison draw", fontsize=16)
    output = folder / "comparison.png"
    figure.savefig(output, dpi=140)
    plt.close(figure)
    print(output)


if __name__ == "__main__":
    main()
