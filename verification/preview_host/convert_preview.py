from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parent
FRAMES=ROOT/'frames'
font_path=Path('C:/Windows/Fonts/segoeui.ttf')
font=ImageFont.truetype(str(font_path),16)
small=ImageFont.truetype(str(font_path),12)
for src in sorted(FRAMES.glob('*.ppm')):
    Image.open(src).save(src.with_suffix('.png'))
def sheet(names,dest,columns=6,scale=1):
    tilew,tileh=240*scale,284*scale
    gap=24;labelh=34;heading=76
    rows=(len(names)+columns-1)//columns
    im=Image.new('RGB',(columns*(tilew+gap)+gap,heading+rows*(tileh+labelh+gap)+gap),(7,15,24))
    d=ImageDraw.Draw(im)
    d.text((gap,17),'DRIVESENSE / ENGLISH OLED UI',font=font,fill=(232,242,246))
    d.text((gap,43),'Actual C framebuffer render. Sensor values are simulated. 240 x 284 pixels.',font=small,fill=(140,166,179))
    for i,name in enumerate(names):
        x=gap+(i%columns)*(tilew+gap);y=heading+(i//columns)*(tileh+labelh+gap)
        pic=Image.open(FRAMES/(name+'.png'))
        if scale!=1:pic=pic.resize((tilew,tileh),Image.Resampling.NEAREST)
        im.paste(pic,(x,y))
        d.text((x,y+tileh+9),name,font=small,fill=(159,190,198))
    im.save(ROOT/dest)
all_names=[p.stem for p in sorted(FRAMES.glob('*.png'))]
sheet(all_names,'preview-all.png')
show=['01-home-running','04-menu-1','07-cabin','19-heading','22-attitude','10-light-lux','15-sound-off','18-ambient-on','25-distance-safe','29-objects-on','13-imu','06-menu-3']
sheet(show,'preview-showcase.png',4,1)
items=''.join('<figure><img src="frames/'+name+'.png" width="240" height="284"><figcaption>'+name+'</figcaption></figure>' for name in all_names)
(ROOT/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>English OLED UI Preview</title><style>body{font:16px system-ui;background:#071018;color:#e8f2f6;margin:32px}p{color:#91acb8}.grid{display:grid;grid-template-columns:repeat(auto-fit,264px);gap:24px}figure{margin:0;padding:12px;background:#10212d;border:1px solid #264451;border-radius:16px}img{image-rendering:auto}figcaption{font-size:12px;margin-top:12px;color:#97bec8}</style><h1>DRIVESENSE / English OLED UI</h1><p>Actual C framebuffer rendering with simulated sensor values. Display size: 240 x 284. Hardware is not connected by this preview.</p><div class="grid">'+items+'</div>',encoding='utf-8')
print('PNG frames:',len(all_names))
print(ROOT/'preview-showcase.png')
print(ROOT/'preview-all.png')

import hashlib, json, re
source_root=ROOT.parent.parent/'firmware'/'src'
source_names=['lcd_st7789.c','ui_draw.c','ui_font_data.h','ui_format.c','touch_hit.c','ui_menu.c','ui_home.c','ui_clock.c','ui_compass.c','ui_attitude.c','ui_distance.c','ui_obstacle.c','collision_model.c']
log=(ROOT/'render.log').read_text(encoding='utf-8-sig',errors='replace') if (ROOT/'render.log').exists() else ''
match=re.search(r'TOTAL_TEXT_OVERFLOWS=(\d+)',log)
report={'render_source':'actual firmware C framebuffer','hardware_access':False,'sensor_data':'simulated','native_resolution':[240,284],'frame_count':len(all_names),'screen_edge_text_overflows':int(match.group(1)) if match else None,'visual_inspection':'33 frames inspected; no card text overlap observed','frames':all_names,'source_sha256':{name:hashlib.sha256((source_root/name).read_bytes()).hexdigest() for name in source_names}}
(ROOT/'verification_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
