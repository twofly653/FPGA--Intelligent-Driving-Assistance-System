from pathlib import Path
from PIL import Image
import struct
root=Path(__file__).parent
buf=(root/'lcd_live_rgb565.bin').read_bytes()
vals=struct.unpack('<%dH'%(len(buf)//2),buf)
pix=[(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v in vals]
im=Image.new('RGB',(240,284)); im.putdata(pix); im.save(root/'lcd_live_native.png'); im.resize((720,852),Image.Resampling.NEAREST).save(root/'lcd_live_preview.png')
print('Rendered real board framebuffer: %d bytes'%len(buf))
