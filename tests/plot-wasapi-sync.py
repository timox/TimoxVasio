"""Plot recorded bridge health; does not infer end-to-end audio latency.

Usage: python tests/plot-wasapi-sync.py SESSION_DIRECTORY OUTPUT_DIRECTORY
Requires matplotlib. Input is the directory containing report.json and
stability-snapshots.jsonl. Outputs PNG, SVG and a reduced, shareable CSV.
"""
import csv
import json
import sys
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

source, output = map(Path, sys.argv[1:3])
report = json.loads((source / 'report.json').read_text(encoding='utf-8-sig'))
baseline = report['stability']['baselineElapsedSeconds']
points = {'render': [], 'endpointLoopback': []}
for line in (source / 'stability-snapshots.jsonl').read_text(encoding='utf-8-sig').splitlines():
    row = json.loads(line)
    if row['elapsedSeconds'] < baseline:
        continue
    for bridge in row['state']['wasapiBridges']:
        kind = bridge['effectiveInfo']['kind']
        if kind in points:
            points[kind].append((row['elapsedSeconds'], bridge['health']['correctionPpm'],
                                 bridge['health']['bufferedFrames'], bridge['graphFormat']['capacityFrames']))
assert all(len(values) == report['stability']['samples'] for values in points.values())
sample_count = len(points['render'])
settled_ppm = [v[1] for values in points.values() for v in values if v[0] >= 120]
signal = report['receive']
rejected = signal['toneDiagnostics']
output.mkdir(parents=True, exist_ok=True)
with (output / 'synchronisation.csv').open('w', newline='', encoding='utf-8') as stream:
    writer = csv.writer(stream)
    writer.writerow(['direction', 'elapsed_seconds', 'correction_ppm', 'buffered_graph_frames', 'capacity_graph_frames'])
    for kind, values in points.items():
        writer.writerows((kind, *value) for value in values)

plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 11,
                     'axes.spines.top': False, 'axes.spines.right': False})
fig, axes = plt.subplots(3, 1, figsize=(13, 10.8), gridspec_kw={'height_ratios': [1.1, 1, 1]})
fig.patch.set_facecolor('#f6f8fb')
colors = {'render': '#1767c0', 'endpointLoopback': '#bd5a18'}
labels = {'render': 'Vers WASAPI (rendu)', 'endpointLoopback': 'Depuis WASAPI (loopback)'}
for kind, values in points.items():
    time = [v[0] for v in values]
    ppm = [v[1] for v in values]
    fill = [v[2] * 100 / v[3] for v in values]
    axes[0].plot(time, ppm, lw=1.25, color=colors[kind], label=labels[kind])
    settled = [v for v in values if v[0] >= 120]
    axes[1].plot([v[0] for v in settled], [v[1] for v in settled], lw=1.1, color=colors[kind])
    axes[2].plot(time, fill, lw=1.1, color=colors[kind])

axes[0].set_title('1. Le réglage du débit converge après le démarrage', loc='left', weight='bold')
for bound in (-2000, 2000):
    axes[0].axhline(bound, color='#9ca3af', ls='--', lw=.8)
axes[0].set(ylabel='Correction appliquée (ppm)', ylim=(-2200, 2400), xlim=(0, 615))
axes[0].legend(loc='upper right', ncol=2, frameon=False, fontsize=10)
axes[1].set_title(f'2. Zoom après 120 s : corrections observées entre {min(settled_ppm):+.0f} et {max(settled_ppm):+.0f} ppm', loc='left', weight='bold')
axes[1].set(ylabel='Correction appliquée (ppm)', ylim=(-200, 200), xlim=(120, 615))
axes[2].set_title('3. Les files restent autour de leur cible de remplissage', loc='left', weight='bold')
axes[2].axhline(50, color='#555f70', ls='--', lw=1, label='Cible : 50 %')
axes[2].set(ylabel='Remplissage FIFO (%)', ylim=(0, 100), xlim=(0, 615))
axes[2].legend(loc='upper right', frameon=False, fontsize=10)
for axis in axes:
    axis.set_xlabel('Temps écoulé depuis le lancement des sondes (s)')
    axis.grid(alpha=.17)
    axis.set_axisbelow(True)
axes[0].axhline(0, color='#9ca3af', lw=.6)
axes[1].axhline(0, color='#9ca3af', lw=.6)
fig.suptitle('Synchronisation ASIO ↔ WASAPI : relevés réels sur 10 minutes',
             x=.075, ha='left', fontsize=19, weight='bold', y=.98)
graph = report['bridgeEvidence'][0]['graphFormat']
fig.text(.075, .935, f"{graph['sampleRate']/1000:g} kHz · quantum {graph['maxQuantum']} frames · {sample_count} relevés par pont après amorçage", color='#475569')
fig.text(.075, .055,
         'Lecture : débit régulé + réserves stables = synchronisation maintenue au niveau des tampons.\n'
         'Les signes sont opposés car les conversions se font dans les deux sens. 100 ppm = 0,01 %.\n'
         'Ce graphique ne mesure ni le retard bout en bout, ni un alignement temporel exact des signaux.',
         fontsize=10, color='#334155')
fig.text(.075, .012,
         f"Présence audio : {signal['matchedToneWindows']} / {signal['toneWindows']} fenêtres valides ; "
         f"{rejected['rejectedWindows']} rejets, plus longue série : {rejected['longestRejectedRun']} fenêtres de 25 ms.\n"
         'Échantillonnage ≈ 1,1 s : les excursions entre relevés ne sont pas observées. Aucun lissage ajouté.',
         fontsize=9, color='#475569')
fig.subplots_adjust(top=.89, bottom=.15, left=.09, right=.97, hspace=.55)
fig.savefig(output / 'synchronisation.png', dpi=150, facecolor=fig.get_facecolor())
fig.savefig(output / 'synchronisation.svg', facecolor=fig.get_facecolor())
print(f'Wrote plots and CSV: {output}; {len(points["render"])} samples per bridge')
