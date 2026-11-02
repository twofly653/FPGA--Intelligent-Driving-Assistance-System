// R16 anti-alias candidate. Output remains 320x240; not a VGA export.
// Frozen analysis snapshot, independent of the DDR/video display ownership.
// Pixel input is complete raster RGB565 in clk/SYS. frame_good describes the
// frame ending on frame_start. arm waits for the NEXT boundary, then captures
// one frame. Bad frames stop with error; only a new arm retries. read_data is
// synchronous, one clk latency; word N contains sampled pixels 2N (low16) and
// 2N+1 (high16). ANTIALIAS=1 averages 2x2; =0 selects even X/Y. No memory reset.
module camera_analysis_buffer #(parameter integer WIDTH=640,HEIGHT=480,ANTIALIAS=1)(
    input wire clk,rst_n,enabled,arm,frame_start,frame_good,pix_valid,
    input wire [19:0] pixel_addr,input wire [15:0] pixel_data,
    input wire [15:0] read_addr,output reg [31:0] read_data,
    output wire [31:0] status
);
    localparam integer PIXELS=WIDTH*HEIGHT;
    localparam integer SAMPLE_PIXELS=(WIDTH/2)*(HEIGHT/2);
    localparam integer WORDS=(SAMPLE_PIXELS+1)/2;
    localparam [1:0] IDLE=0,WAIT_FRAME=1,CAPTURE=2,FROZEN=3;
    reg [1:0] state;
    reg ready,error,bad;
    reg [15:0] generation;
    reg [19:0] input_count,sample_count;
    reg [15:0] x,y,write_addr;
    reg [15:0] low_pixel;
    reg half_word;
    // Explicit 1024x32 banks fit a single RAMB36 each and avoid inferred
    // deep-cascade ADDR15 rewriting. Same synchronous one-clock read contract.
    localparam integer BANK_WORDS=1024;
    localparam integer BANKS=(WORDS+BANK_WORDS-1)/BANK_WORDS;
    wire [31:0] bank_q[0:BANKS-1];
    wire accepted=state==CAPTURE && enabled && !frame_start && pix_valid &&
                  !bad && pixel_addr==input_count && input_count<PIXELS;
    // Re-arm may write one discarded old-frame word; state immediately clears
    // ready and every word is overwritten before the next FROZEN publication.
    // No combinational AXI arm path is needed on the BRAM write-enable pins.

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
    wire sample=accepted && (ANTIALIAS?(x[0] && y[0]):(!x[0] && !y[0]));
    wire write_word=sample && (half_word || sample_count==SAMPLE_PIXELS-1);
    wire [31:0] write_value=half_word?{sampled_pixel,low_pixel}:{16'd0,sampled_pixel};
    wire can_read=state==FROZEN && enabled && read_addr<WORDS;
    genvar bank;
    generate for(bank=0;bank<BANKS;bank=bank+1)begin:ANALYSIS_BANK
        (* keep_hierarchy="yes" *) camera_analysis_ram_bank ram(
            .clk(clk),.we(write_word && write_addr<WORDS && (write_addr>>10)==bank),
            .waddr(write_addr[9:0]),.data(write_value),
            .re(can_read && (read_addr>>10)==bank),.raddr(read_addr[9:0]),.q(bank_q[bank]));
    end endgenerate
    integer read_bank;
    always @* begin
        read_data=0;
        if(state==FROZEN && enabled)
            for(read_bank=0;read_bank<BANKS;read_bank=read_bank+1)read_data=read_data|bank_q[read_bank];
    end
    always @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            state<=IDLE;ready<=0;error<=0;bad<=0;generation<=0;
            input_count<=0;sample_count<=0;x<=0;y<=0;write_addr<=0;low_pixel<=0;half_word<=0;
        end else if(!enabled)begin
            state<=IDLE;ready<=0;error<=0;bad<=0;input_count<=0;sample_count<=0;
            x<=0;y<=0;write_addr<=0;half_word<=0;
        end else if(arm)begin
            state<=WAIT_FRAME;ready<=0;error<=0;bad<=0;
            input_count<=0;sample_count<=0;x<=0;y<=0;write_addr<=0;half_word<=0;
        end else if(state==WAIT_FRAME && frame_start)begin
            state<=CAPTURE;bad<=0;input_count<=0;sample_count<=0;
            x<=0;y<=0;write_addr<=0;half_word<=0;
        end else if(state==CAPTURE)begin
            if(frame_start)begin
                if(frame_good && !bad && input_count==PIXELS && sample_count==SAMPLE_PIXELS && !half_word)begin
                    ready<=1;error<=0;state<=FROZEN;generation<=generation+1'b1;
                end else begin ready<=0;error<=1;state<=IDLE;end
            end else if(pix_valid)begin
                if(!accepted)bad<=1;
                else begin
                    input_count<=input_count+1'b1;
                    if(x==WIDTH-1)begin x<=0;y<=y+1'b1;end else x<=x+1'b1;
                    if(sample)begin
                        sample_count<=sample_count+1'b1;
                        if(write_word)begin write_addr<=write_addr+1'b1;half_word<=0;end
                        else begin low_pixel<=sampled_pixel;half_word<=1;end
                    end
                end
            end
        end
    end
    assign status={generation,13'd0,error,(state==WAIT_FRAME || state==CAPTURE),ready};
endmodule

// One physical-size bank: never a depth cascade. Disabled banks publish zero,
// so a balanced OR network selects the single synchronously enabled output.
module camera_analysis_ram_bank(
    input wire clk,we,re,input wire [9:0] waddr,raddr,
    input wire [31:0] data,output reg [31:0] q
);
    (* ram_style="block" *) reg [31:0] memory[0:1023];
    always @(posedge clk)begin
        if(we)memory[waddr]<=data;
        if(re)q<=memory[raddr];else q<=0;
    end
endmodule
