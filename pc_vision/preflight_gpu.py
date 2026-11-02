from pathlib import Path
from datetime import datetime
import json
import torch

root=Path(__file__).resolve().parent
(root/'validation').mkdir(exist_ok=True)
assert torch.cuda.is_available(), 'CUDA unavailable'
x=torch.ones((512,512),device='cuda:0',dtype=torch.float16)
y=x@x
torch.cuda.synchronize()
assert torch.isfinite(y).all().item() and y[0,0].item()==512
record={'timestamp_local':datetime.now().astimezone().isoformat(),'torch':torch.__version__,
        'cuda':torch.version.cuda,'device':torch.cuda.get_device_name(0),
        'compute_capability':torch.cuda.get_device_capability(0),
        'compiled_architectures':torch.cuda.get_arch_list(),
        'half_precision_gpu_matmul_verified':True,'cpu_fallback':False,
        'vram_total_bytes':torch.cuda.get_device_properties(0).total_memory}
(root/'validation/gpu_environment.json').write_text(json.dumps(record,indent=2))
print(json.dumps(record,indent=2))
