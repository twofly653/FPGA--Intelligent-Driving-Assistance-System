from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import math
root=Path(__file__).resolve().parents[1]
fonts=Path(__file__).resolve().parent/'fonts'
out=root/'firmware/src/ui_font_data.h'
lines=['/* Rasterized from Bitstream Vera. License: verification/fonts/bitstream-vera-license.txt. */','#ifndef UI_FONT_DATA_H','#define UI_FONT_DATA_H','typedef struct { unsigned height; const unsigned *offset; const unsigned char *width; const unsigned char *pixels; } ui_font_data_t;']
for index,(height,size,bold) in enumerate([(12,10,False),(16,13,False),(20,17,True),(28,24,True),(40,34,False)]):
 font=ImageFont.truetype(str(fonts/('VeraBd.ttf' if bold else 'Vera.ttf')),size)
 widths=[];offsets=[];data=[]
 for code in range(32,127):
  ch=chr(code);width=max(2,math.ceil(font.getlength(ch))+1)
  im=Image.new('L',(width,height));d=ImageDraw.Draw(im)
  d.text((0,height-3),ch,font=font,fill=255,anchor='ls')
  offsets.append(len(data));widths.append(width)
  data.extend((int(v+8)//17)*17 if v<248 else 255 for v in im.getdata())
 lines.append('static const unsigned ui_font_offset_%d[95]={%s};'%(index,','.join(map(str,offsets))))
 lines.append('static const unsigned char ui_font_width_%d[95]={%s};'%(index,','.join(map(str,widths))))
 lines.append('static const unsigned char ui_font_pixels_%d[%d]={\n%s\n};'%(index,len(data),'\n'.join(','.join(map(str,data[i:i+48]))+',' for i in range(0,len(data),48))))
lines.append('static const ui_font_data_t ui_fonts[5]={'+','.join('{%d,ui_font_offset_%d,ui_font_width_%d,ui_font_pixels_%d}'%(height,i,i,i) for i,height in enumerate([12,16,20,28,40]))+'};\n#endif\n')
out.write_text('\n'.join(lines),encoding='ascii')
print('Generated',out)
