"""Export every sheet of data/raw/field_project_all_readings.xlsx to CSV and draw figures.
Run from the repo root:  python scripts/export_and_plot.py
"""
import openpyxl, csv, datetime as dt, re
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

SRC = "data/raw/field_project_all_readings.xlsx"
OUT = "data/processed"
wb = openpyxl.load_workbook(SRC, data_only=True)

def slug(s): return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")
def mins(t): return t.hour * 60 + t.minute + t.second / 60

tables = {}
for ws in wb:
    rows = [r for r in ws.iter_rows(values_only=True) if any(c is not None for c in r)]
    header, data = rows[2], rows[3:]
    tables[ws.title] = data
    with open(f"{OUT}/{slug(ws.title)}.csv", "w", newline="") as f:
        w = csv.writer(f); w.writerow(header)
        for r in data:
            w.writerow([c.strftime("%H:%M:%S") if isinstance(c, dt.time) else ("" if c is None else c) for c in r])

plt.rcParams.update({"figure.figsize": (8, 4), "axes.grid": True, "grid.alpha": .3})
def save(name): plt.tight_layout(); plt.savefig(f"figures/{name}.png", dpi=150); plt.close()

d = tables["GAC Test MQ135"]; x = [mins(r[0]) - mins(d[0][0]) for r in d]
plt.plot(x, [r[1] for r in d], marker="o", ms=3, color="#7a1424")
plt.xlabel("minutes since baseline"); plt.ylabel("MQ135 raw")
plt.title("GAC test: spray spike and charcoal decay (12 Aug)")
save("gac_decay")

for k, c in (("Perfume - On Moss SGP30", "#2e7d32"), ("Perfume - Without Moss SGP30", "#7a1424")):
    d = tables[k]; x = [mins(r[0]) - mins(d[0][0]) for r in d]
    plt.plot(x, [r[1] for r in d], marker="o", ms=3, color=c, label=k.split(" SGP30")[0].replace("Perfume - ", ""))
plt.xlabel("minutes since baseline"); plt.ylabel("TVOC (ppb)"); plt.legend()
plt.title("Perfume spray: TVOC decay, moss vs no moss")
save("perfume_moss_vs_none")

d = tables["Room AQI & VOC Log"]; x = [mins(r[0]) for r in d]
fig, ax = plt.subplots(4, 1, figsize=(8, 8), sharex=True)
for a, i, n in zip(ax, (1, 2, 3, 4), ("MQ135 raw", "PM2.5", "CO2 (ppm)", "Temp (°C)")):
    a.plot(x, [r[i] for r in d], color="#7a1424"); a.set_ylabel(n)
ax[-1].set_xlabel("minutes since midnight (12 Aug, 15:49-16:43)")
save("room_log")

d = tables["Incense Test MQ135"]
plt.plot([mins(r[0]) for r in d], [r[1] for r in d], marker="o", ms=3, color="#7a1424")
plt.axhline(4095, ls=":", c="gray")
plt.xlabel("minutes since midnight (4 Aug)"); plt.ylabel("MQ135 raw"); plt.title("Incense stick test (dotted = 12-bit ADC ceiling)")
save("incense")
print("done")
