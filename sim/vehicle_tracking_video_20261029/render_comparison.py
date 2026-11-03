"""Render actual replay boxes at their replay rate; no interpolated detections."""
from pathlib import Path
import hashlib, json, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont

H = Path(__file__).resolve().parent
F = Path(r'D:\Software for daily live\EVCapture\ffmpeg.exe')
FONT = Path('C:/Windows/Fonts/msyh.ttc')
font = ImageFont.truetype(str(FONT), 22)
small = ImageFont.truetype(str(FONT), 17)
fps = 10
data = H / 'data/fps10_stable'
manifest = json.loads((data / 'manifest.json').read_text())
n = manifest['frames']
raw = np.memmap(data / 'frames.rgb565', dtype='<u2', mode='r', shape=(n,240,320))
outputs = {v: json.loads((H / 'results' / (v + '_fps10_stable.json')).read_text())
           for v in ('baseline', 'candidate')}
dest = H / 'results/R12_R13_video_comparison.mp4'
cmd = [str(F), '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
       '-s', '1280x480', '-r', str(fps), '-i', '-', '-an', '-c:v', 'libx264',
       '-preset', 'fast', '-crf', '18', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(dest)]
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
try:
    for i, pixels in enumerate(raw):
        r=(pixels>>11)&31; g=(pixels>>5)&63; b=pixels&31
        im = Image.fromarray(np.stack(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)),2).astype('uint8'))
        canvas = Image.new('RGB', (1280,480), (15,22,32))
        draw = ImageDraw.Draw(canvas)
        draw.text((20,12), '车辆跟踪离线对比 · 同一段原视频 / 同一输入 / 同一采样时间', font=font, fill='white')
        for k, v in enumerate(('baseline','candidate')):
            row = outputs[v][i]
            pane = im.crop((0,47,320,193)).resize((640,292), Image.Resampling.NEAREST)
            pd = ImageDraw.Draw(pane)
            for q in row['objects']:
                x=2*q['x']; y=2*(q['y']-47); w=2*q['w']; h=2*q['h']
                pd.rectangle((x,y,x+w-1,y+h-1), outline=(30,255,110), width=2)
                pd.text((x+2,max(0,y-18)), str(q['id']), font=small, fill='yellow', stroke_width=1, stroke_fill='black')
            canvas.paste(pane,(k*640,90))
            title = 'R12 原版本' if v=='baseline' else 'R13 优化版本'
            draw.text((20+k*640,53), f'{title}   当前输出 {row["count"]} 个框', font=font, fill=(100,220,255))
        draw.text((20,397), f'原视频 {outputs["candidate"][i]["source_time_s"]:.1f}s  |  按 10 次/秒真实回放  |  分析输入 320×240', font=small, fill='white')
        draw.text((20,429), '绿色框为程序实际输出；ID 为关联编号。仍有遮挡合框与漏检。此视频不代表板上帧率或端到端延迟。', font=small, fill=(190,199,210))
        proc.stdin.write(canvas.tobytes())
        if i == 45:
            canvas.save(H / 'results/r12_r13_preview.png')
finally:
    proc.stdin.close()
error = proc.stderr.read().decode('utf-8', errors='replace')
assert proc.wait() == 0, error
(H / 'results/comparison_manifest.json').write_text(json.dumps({
    'video': str(dest), 'sha256': hashlib.sha256(dest.read_bytes()).hexdigest(),
    'fps': fps, 'frames': n, 'source_start_s': 5.5, 'source_end_s': 15.0,
    'input_size': [320,240], 'visualization': 'Active input rows 47..192 magnified; boxes from actual replay JSON, no interpolation.',
    'command': cmd,
    'replay_sha256': {v: hashlib.sha256((H/'results'/(v+'_fps10_stable.json')).read_bytes()).hexdigest() for v in outputs}
}, indent=2)+'\n', encoding='utf-8')
print(dest)
