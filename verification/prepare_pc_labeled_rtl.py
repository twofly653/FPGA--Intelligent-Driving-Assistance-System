"""Prepare two source variants for a future OB02 build. No project or board access."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REFERENCE=ROOT.parent/'camera_optimized_20261025/rtl'
OUT=ROOT/'rtl';OUT.mkdir(exist_ok=True)
overlay=(REFERENCE/'camera/camera_obstacle_overlay.v').read_text(encoding='utf-8-sig')
overlay='// R15 OB02 candidate: optional five-digit IDs in existing mailbox spare bits.\n'+overlay
overlay=overlay.replace('reg [7:0] pending_mask,active_mask;','reg [7:0] pending_mask,active_mask;\n    reg pending_labels,active_labels;')
overlay=overlay.replace('input [63:0] pair;','input [63:0] pair;\n        input labels;')
overlay=overlay.replace('((pair & 64\'hFC00FC00FC00FC00)==0)','(labels || (pair & 64\'hFC00FC00FC00FC00)==0)')
overlay=overlay.replace('valid_box(sampled_boxes[i*64+:64])','valid_box(sampled_boxes[i*64+:64],sampled_control[9])')
overlay=overlay.replace('pending_mask<=0;active_mask<=0;','pending_mask<=0;active_mask<=0;pending_labels<=0;active_labels<=0;')
overlay=overlay.replace('pending_boxes<=sampled_boxes;pending_mask<=sampled_mask;','pending_labels<=sampled_control[9];pending_boxes<=sampled_boxes;pending_mask<=sampled_mask;')
overlay=overlay.replace('active_boxes<=pending_boxes;active_mask<=pending_mask;','active_labels<=pending_labels;active_boxes<=pending_boxes;active_mask<=pending_mask;')
glyphs=['01110100011001110101110011000101110','00100011000010000100001000010001110',
        '01110100010000100010001000100011111','11110000010000101110000010000111110',
        '00010001100101010010111110001000010','11111100001000011110000010000111110',
        '01110100001000011110100011000101110','11111000010001000100010000100001000',
        '01110100011000101110100011000101110','01110100011000101111000010000101110']
font='''
    function digit_pixel;
        input integer digit,row,column;
        reg [34:0] bits;
        begin
            case(digit)
'''+''.join(f"            {i}:bits=35'b{v};\n" for i,v in enumerate(glyphs))+'''            default:bits=0;
            endcase
            digit_pixel=row<7 && column<5 ? bits[34-(row*5+column)] : 1'b0;
        end
    endfunction
    integer k,character,label_x,label_y,lcol,lrow;
    integer label_commit_i;
    reg [19:0] active_digits[0:7];
    function [19:0] decimal_digits;
        input [15:0] id;
        begin
            decimal_digits[19:16]=id/10000;
            decimal_digits[15:12]=(id/1000)%10;
            decimal_digits[11:8]=(id/100)%10;
            decimal_digits[7:4]=(id/10)%10;
            decimal_digits[3:0]=id%10;
        end
    endfunction
    reg label_pixel,label_background;
    reg [15:0] label_id;
    always @* begin
        label_pixel=0;label_background=0;label_x=0;label_y=0;lcol=0;lrow=0;
        label_id=0;
        for(k=0;k<8;k=k+1)begin
            label_id={active_boxes[k*64+42+:4],active_boxes[k*64+26+:6],active_boxes[k*64+10+:6]};
            label_x=active_boxes[k*64+:10];if(label_x>WIDTH-30)label_x=WIDTH-30;
            label_y=active_boxes[k*64+16+:10];if(label_y>=9)label_y=label_y-9;else label_y=label_y+2;
            if(active_labels && overlay_visible && active_mask[k] && label_id!=0 && in_de &&
               in_x>=label_x && in_x<label_x+30 && in_y>=label_y && in_y<label_y+7)begin
                label_background=1;lrow=in_y-label_y;
                for(character=0;character<5;character=character+1)begin
                    lcol=in_x-label_x-character*6;
                    if(lcol>=0 && lcol<5 && digit_pixel(active_digits[k][(4-character)*4+:4],lrow,lcol))label_pixel=1;
                end
            end
        end
    end
'''
overlay=overlay.replace('    always @(posedge clk or negedge rst_n) begin\n        if(!rst_n) begin output_rgb',font+'    always @(posedge clk or negedge rst_n) begin\n        if(!rst_n) begin output_rgb')
overlay=overlay.replace('pending_mask<=0;active_mask<=0;pending_labels<=0;active_labels<=0;',
    'pending_mask<=0;active_mask<=0;pending_labels<=0;active_labels<=0;\n            for(label_commit_i=0;label_commit_i<8;label_commit_i=label_commit_i+1)active_digits[label_commit_i]<=0;')
overlay=overlay.replace('active_labels<=pending_labels;active_boxes<=pending_boxes;',
    'active_labels<=pending_labels;active_boxes<=pending_boxes;\n                        for(label_commit_i=0;label_commit_i<8;label_commit_i=label_commit_i+1)\n                            active_digits[label_commit_i]<=decimal_digits({pending_boxes[label_commit_i*64+42+:4],pending_boxes[label_commit_i*64+26+:6],pending_boxes[label_commit_i*64+10+:6]});')
overlay=overlay.replace("output_rgb<=edge_pixel?24'h00FF00:input_rgb;","output_rgb<=label_pixel?24'hFFFFFF:label_background?24'h006000:edge_pixel?24'h00FF00:input_rgb;")
# Keep the derivative valid as Verilog-2001, matching the original .v project.
start=overlay.index('    integer label_commit_i;')
end=overlay.index('    reg label_pixel,label_background;')
declarations=overlay[start:end]
overlay=overlay[:start]+overlay[end:]
overlay=overlay.replace('    integer i;',declarations+'    integer i;')
assert 'digit_pixel' in overlay and 'active_labels<=pending_labels' in overlay
(OUT/'camera_obstacle_overlay.v').write_text(overlay,encoding='utf-8')
hub=(REFERENCE/'axi_pl_hub.v').read_text(encoding='utf-8-sig').replace("32'h03FF010F","32'h03FF030F").replace("32'h4F423031","32'h4F423032")
hub=hub.replace('// OB01, does not change TV05 compatibility','// OB02 label metadata; retains TV05 compatibility')
(OUT/'axi_pl_hub.v').write_text('// R15 OB02 candidate: retains label-mode control bit9.\n'+hub,encoding='utf-8')
print('Prepared OB02 source variants; no BIT generated and no flashing performed.')
