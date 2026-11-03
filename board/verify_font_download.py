from pathlib import Path
import struct,json
r=Path(__file__).parents[1]
e=(r/'src/firmware/release/vehicle_assist_lcd_english.elf').read_bytes()
phoff=struct.unpack_from('<I',e,28)[0];size,num=struct.unpack_from('<HH',e,42)
addr=0x153440;n=7860;expected=None
for i in range(num):
 t,off,v,p,fs,ms,fl,al=struct.unpack_from('<8I',e,phoff+i*size)
 if t==1 and v<=addr and addr+n<=v+fs: expected=e[off+addr-v:off+addr-v+n]
assert expected is not None
live=(r/'board/font_small_live.bin').read_bytes()
diff=[i for i,(a,b) in enumerate(zip(expected,live)) if a!=b]
report={'address':hex(addr),'bytes':n,'read_bytes':len(live),'different_bytes':len(diff),'match':len(live)==n and not diff}
(r/'board/font_verification.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report)); assert report['match']
