"""Write an isolated analysis-filter RTL candidate; no project/board changes."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
source=(r.parent/'camera_optimized_20261025/rtl/camera/camera_analysis_buffer.v').read_text(encoding='utf-8-sig')
source=source.replace('parameter integer WIDTH=640,HEIGHT=480','parameter integer WIDTH=640,HEIGHT=480,ANTIALIAS=1')
insert='''
    // 2x2 box filter in native RGB565 components, before decimation.
    // Horizontal sums occupy one half-width distributed line store (6080 bits
    // for VGA). No frame queue or extra frame delay. First complete even row
    // overwrites this unreset storage before the corresponding odd row reads.
    reg [15:0] left_pixel;
    (* ram_style="distributed" *) reg [18:0] top_pairs[0:WIDTH/2-1];
    wire [5:0] pair_r={1'b0,left_pixel[15:11]}+{1'b0,pixel_data[15:11]};
    wire [6:0] pair_g={1'b0,left_pixel[10:5]}+{1'b0,pixel_data[10:5]};
    wire [5:0] pair_b={1'b0,left_pixel[4:0]}+{1'b0,pixel_data[4:0]};
    wire [18:0] top_pair=top_pairs[x>>1];
    wire [6:0] total_r={1'b0,pair_r}+{1'b0,top_pair[18:13]}+7'd2;
    wire [7:0] total_g={1'b0,pair_g}+{1'b0,top_pair[12:6]}+8'd2;
    wire [6:0] total_b={1'b0,pair_b}+{1'b0,top_pair[5:0]}+7'd2;
    wire [15:0] sampled_pixel=ANTIALIAS?{total_r[6:2],total_g[7:2],total_b[6:2]}:pixel_data;
    always @(posedge clk)begin
        if(accepted && ANTIALIAS)begin
            if(!x[0])left_pixel<=pixel_data;
            else if(!y[0])top_pairs[x>>1]<={pair_r,pair_g,pair_b};
        end
    end
'''
source=source.replace('    wire sample=accepted && !x[0] && !y[0];',insert+'    wire sample=accepted && (ANTIALIAS?(x[0] && y[0]):(!x[0] && !y[0]));')
source=source.replace('half_word?{pixel_data,low_pixel}:{16\'d0,pixel_data}','half_word?{sampled_pixel,low_pixel}:{16\'d0,sampled_pixel}')
source=source.replace('low_pixel<=pixel_data;','low_pixel<=sampled_pixel;')
source=source.replace('Even source X/Y form WIDTH/2 by HEIGHT/2. No memory reset.','ANTIALIAS=1 averages 2x2; =0 selects even X/Y. No memory reset.')
assert source.count('sampled_pixel')==4
source='// R16 anti-alias candidate. Output remains 320x240; not a VGA export.\n'+source
(r/'src/rtl/camera_analysis_buffer.v').write_text(source,encoding='utf-8')
print('ANTIALIAS_RTL_PREPARED_NO_BUILD_NO_FLASH')
