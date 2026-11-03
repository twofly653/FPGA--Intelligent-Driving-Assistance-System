"""Inspect saved exploratory results; experiments are historical and not rerun.

Earlier revisions used source-string mutations. Those mutations no longer apply
reliably after the chosen variant was merged. This entry point intentionally
never compiles or overwrites ablation results. Reproduce the supported final
comparison with video_regression.py followed by score_sparse.py instead.
"""
from pathlib import Path
import json

here = Path(__file__).resolve().parent
print('Historical exploration only. Final reproducible comparison: video_regression.py + score_sparse.py')
p = here / 'results/ablation_summary.json'
if p.exists():
    for result in json.loads(p.read_text()):
        print(result)
