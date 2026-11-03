// R15 OB02 candidate: retains label-mode control bit9.
//////////////////////////////////////////////////////////////////////////////
// axi_pl_hub.v
// AXI-Lite hub for vehicle_system (from lab6):
//   buttons, 7-seg, buzzer, WS2812, LCD GPIO, 5-way/encoder,
//   I2C bit-bang (all Attach sensors), PDM mic, CST816 RST/INT
// Base address: Vivado Address Editor (typical 0x43C0_0000)
// I2C IOBUF must stay in vehicle_top.v (not here) � OOC readback issue.
//////////////////////////////////////////////////////////////////////////////

module axi_pl_hub #(
    parameter C_S_AXI_DATA_WIDTH = 32,
    parameter C_S_AXI_ADDR_WIDTH = 8,
    parameter CLK_HZ             = 100_000_000
) (
    input  wire         s_axi_aclk,
    input  wire         s_axi_aresetn,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0] s_axi_awaddr,
    input  wire [2:0]   s_axi_awprot,
    input  wire         s_axi_awvalid,
    output wire         s_axi_awready,
    input  wire [C_S_AXI_DATA_WIDTH-1:0] s_axi_wdata,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] s_axi_wstrb,
    input  wire         s_axi_wvalid,
    output wire         s_axi_wready,
    output wire [1:0]   s_axi_bresp,
    output wire         s_axi_bvalid,
    input  wire         s_axi_bready,
    input  wire [C_S_AXI_ADDR_WIDTH-1:0] s_axi_araddr,
    input  wire [2:0]   s_axi_arprot,
    input  wire         s_axi_arvalid,
    output wire         s_axi_arready,
    output wire [C_S_AXI_DATA_WIDTH-1:0] s_axi_rdata,
    output wire [1:0]   s_axi_rresp,
    output wire         s_axi_rvalid,
    input  wire         s_axi_rready,

    input  wire [3:0]   btn,            // PYNQ BTN3..BTN0, active high
    output wire [3:0]   led,            // PYNQ LD3..LD0 debug
    output wire [7:0]   seg,
    output wire [8:0]   dig,
    output wire         ws2812_din,
    output wire         buzzer,
    output wire         lcd_sck,
    output wire         lcd_mosi,
    output wire         lcd_dc,
    output wire         lcd_cs,
    output wire         lcd_rst,
    output wire         lcd_blk,
    /* Attach K1 ����: active-low when pressed (internal invert) */
    input  wire         key_n,
    input  wire         key_s,
    input  wire         key_w,
    input  wire         key_e,
    input  wire         key_c,
    /* Attach K2 encoder */
    input  wire         enc_a,
    input  wire         enc_b,
    input  wire         enc_pb,
    /* I2C open-drain split: T/I only (IOBUF must live at true top, not OOC IP) */
    output wire         i2c_scl_t,   /* 1 = release (Hi-Z), 0 = drive 0 */
    output wire         i2c_sda_t,
    input  wire         i2c_scl_i,
    input  wire         i2c_sda_i,
    output wire         dmic_mclk,
    input  wire         dmic_mdat,
    /* CST816 touch: RST out, INT in (Attach LCD_TP_*) */
    output wire         tp_rst,
    input  wire         tp_int,

    /* 100 MHz / reset for top-level camera_hdmi_top (not synthesized OOC) */
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 pl_clk CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME pl_clk, FREQ_HZ 100000000, ASSOCIATED_RESET pl_rstn" *)
    output wire         pl_clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 pl_rstn RST" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME pl_rstn, POLARITY ACTIVE_LOW" *)
    output wire         pl_rstn,
    input wire [31:0]   camera_diag0,
    input wire [31:0]   camera_diag1,
    input wire [31:0]   camera_diag2,
    input wire [31:0]   camera_diag3,
    input wire [31:0]   camera_diag4,
    input wire [31:0]   camera_diag5,
    input wire [31:0]   camera_diag6,
    input wire [31:0]   camera_diag7,
    output reg [31:0]   camera_control,
    input wire [31:0] jpeg_status,jpeg_length,jpeg_fifo_data,jpeg_fifo_level,
    input wire [31:0] camera_ddr_status,
    output wire jpeg_fifo_pop,
    output wire analysis_arm,
    output reg [15:0] analysis_read_addr,
    input wire [31:0] analysis_status, analysis_read_data,
    input wire [31:0] sccb_read_status,
    output reg [31:0] traffic_display,
    output reg [511:0] obstacle_boxes,
    output reg [31:0] obstacle_control, obstacle_source, obstacle_sequence,
    output reg obstacle_req,
    input wire obstacle_ack
);

wire clk   = s_axi_aclk;
wire rst_n = s_axi_aresetn;

//-----------------------------------------------------------------------------
// AXI-Lite handshake (Xilinx simple slave style)
//-----------------------------------------------------------------------------
reg awready_r, wready_r, bvalid_r;
reg arready_r, rvalid_r;
reg [C_S_AXI_ADDR_WIDTH-1:0] awaddr_r, araddr_r;
reg [31:0] rdata_r;

wire        lcd_tx_ready, lcd_tx_busy;
wire [7:0]  lcd_tx_free;
wire        lcd_tx_sck, lcd_tx_mosi;
/* The serializer consumes one RGB565 pixel per write. Never acknowledge a
 * full FIFO: the AXI master retains address/data until space is available. */
wire lcd_write_space = (s_axi_awaddr[7:2] != 6'h13) || lcd_tx_ready;
wire write_response_space = !bvalid_r || s_axi_bready;

assign s_axi_awready = awready_r;
assign s_axi_wready  = wready_r;
assign s_axi_bvalid  = bvalid_r;
assign s_axi_bresp   = 2'b00;
assign s_axi_arready = arready_r;
assign s_axi_rvalid  = rvalid_r;
assign s_axi_rdata   = rdata_r;
assign s_axi_rresp   = 2'b00;

always @(posedge clk) begin
    if (!rst_n) begin
        awready_r <= 1'b0;
        awaddr_r  <= {C_S_AXI_ADDR_WIDTH{1'b0}};
    end else begin
        if (~awready_r && s_axi_awvalid && s_axi_wvalid &&
            lcd_write_space && write_response_space) begin
            awready_r <= 1'b1;
            awaddr_r  <= s_axi_awaddr;
        end else begin
            awready_r <= 1'b0;
        end
    end
end

always @(posedge clk) begin
    if (!rst_n)
        wready_r <= 1'b0;
    else if (~wready_r && s_axi_wvalid && s_axi_awvalid &&
             lcd_write_space && write_response_space)
        wready_r <= 1'b1;
    else
        wready_r <= 1'b0;
end

wire wr_en = wready_r && s_axi_wvalid && awready_r && s_axi_awvalid;
wire [5:0] wr_sel = s_axi_awaddr[7:2];
// A new frozen analysis frame is requested only by one accepted full write.
assign analysis_arm = rst_n && wr_en && wr_sel==6'h24 && s_axi_wstrb[0] && s_axi_wdata[0];

always @(posedge clk) begin
    if (!rst_n)
        bvalid_r <= 1'b0;
    else if (wr_en)
        bvalid_r <= 1'b1;
    else if (s_axi_bready && bvalid_r)
        bvalid_r <= 1'b0;
end

always @(posedge clk) begin
    if (!rst_n) begin
        arready_r <= 1'b0;
        araddr_r  <= {C_S_AXI_ADDR_WIDTH{1'b0}};
    end else begin
        if (~arready_r && s_axi_arvalid && !rvalid_r) begin
            arready_r <= 1'b1;
            araddr_r  <= s_axi_araddr;
        end else begin
            arready_r <= 1'b0;
        end
    end
end

wire rd_en = arready_r && s_axi_arvalid && ~rvalid_r;
wire [5:0] rd_sel = s_axi_araddr[7:2];
// Exactly one pop per accepted AXI address, independent of R-channel stalls.
assign jpeg_fifo_pop=rst_n && rd_en && rd_sel==6'h21 && jpeg_fifo_level!=0;

always @(posedge clk) begin
    if (!rst_n)
        rvalid_r <= 1'b0;
    else if (rd_en)
        rvalid_r <= 1'b1;
    else if (rvalid_r && s_axi_rready)
        rvalid_r <= 1'b0;
end

//-----------------------------------------------------------------------------
// User registers
//-----------------------------------------------------------------------------
// 0x00 STATUS     R    [3:0] btn_level  [7:4] btn_sticky(3..0)
//                      [8] buz_busy   [9] ws_busy
// 0x04 STATUS_CLR W    write 1 to [7:4] clears corresponding sticky
// 0x08 SEG_BRIGHT W/R  0..100
// 0x0C SEG_BLANK  W/R  phys blank[8:0]  (1 = off). phys[0]=leftmost
// 0x10 SEG_HEX0   W/R  phys digits 0..7, nibble each
// 0x14 SEG_HEX1   W/R  phys digit 8 in [3:0]
// 0x18 BUZ_CTRL   W    write 1 to start
// 0x1C BUZ_FREQ   W/R  Hz
// 0x20 BUZ_MS     W/R  duration ms
// 0x24 WS_CTRL    W    [1:0] mode, write any to this addr starts effect
// 0x28 WS_COLOR   W/R  GRB
// 0x2C WS_BRIGHT  W/R  0..100
// 0x30 LCD_GPIO   W/R  {blk,rst,cs,dc,mosi,sck}
// 0x34 KEY_STAT   R    [4:0] key level N,S,W,E,C (active high=pressed)
//                      [12:8] key sticky press
// 0x38 ENC_STAT   R    [0]A [1]B [2]PB level(pressed)
//                      [8]CW sticky [9]CCW sticky [10]PB sticky
// 0x40 I2C_GPIO   W/R  write [0]scl_oe [1]sda_oe (1=drive 0)
//                      read  [0]scl_in [1]sda_in [8]scl_oe [9]sda_oe
// 0x44 MIC_STAT   R    [7:0] db  [23:8] amp
// 0x48 TP_GPIO    W/R  [0] reset output; read [8] interrupt input
// 0x4C LCD_TX     W    [15:0] RGB565, MSB first; low two WSTRB required
// 0x50 LCD_TX_STAT R   [31:16] 0x4C53, [15:8] free FIFO slots,
//                      [1] full, [0] busy (including final falling edge)
// 0x54 DEBUG_LED  W/R  [0] enable old debug LEDs; default 0 = quiet

reg [7:0]  seg_bright;
reg [5:0]  lcd_gpio;
reg        tp_rst_r;
reg        debug_led_enable;
reg [8:0]  phys_blank;
reg [3:0]  phys_hex [0:8];
reg [15:0] buz_freq;
reg [15:0] buz_ms;
reg [2:0]  ws_mode;
reg [1:0]  i2c_oe;          // [0]scl [1]sda, 1 = drive 0
wire [15:0] mic_amp;
wire [7:0]  mic_db;
reg [23:0] ws_color;
reg [7:0]  ws_bright;
reg        buz_start;
reg        ws_start;
reg [3:0]  btn_sticky;

wire       buz_busy;
wire       ws_busy;
wire [3:0] btn_press;
wire [3:0] btn_level;

wire [4:0] key_level;     // N S W E C, pressed=1
wire [4:0] key_press;
reg  [4:0] key_sticky;
wire       enc_a_s, enc_b_s, enc_pb_s;
reg        enc_cw_sticky, enc_ccw_sticky, enc_pb_sticky;
reg        enc_clr_cw, enc_clr_ccw, enc_clr_pb;

// Obstacle result mailbox. Staging writes never modify an in-flight payload.
// 0xAC COMMIT full-WSTRB: enable[8], count[3:0] (0..8), initial_age_ms[25:16]. Busy commits are
// rejected without blocking AXI; software checks F0[0] before staging a result.
// B0..EC: eight {XY_MIN,XY_MAX} pairs, x[9:0], y[25:16], inclusive VGA coords.
// F0 R: [0]busy [1]ack [2]rejected_commit [7:4]mailbox_count [8]mailbox_enable
//       [31:16]accepted_sequence_low16. W1C bit2 clears rejected_commit.
// F4 R: OB01 feature identity. F8/FC R/W: staging source-frame/result-sequence.
reg [511:0] obstacle_staging_boxes;
reg [31:0] obstacle_staging_source, obstacle_staging_sequence;
(* ASYNC_REG="TRUE" *) reg obstacle_ack_meta, obstacle_ack_sync;
reg obstacle_rejected;
wire obstacle_busy = obstacle_req != obstacle_ack_sync;
integer ob_byte;
always @(posedge clk) begin
    if(!rst_n) begin
        obstacle_staging_boxes<=0;obstacle_staging_source<=0;obstacle_staging_sequence<=0;
        obstacle_boxes<=0;obstacle_control<=0;obstacle_source<=0;obstacle_sequence<=0;
        obstacle_req<=0;obstacle_ack_meta<=0;obstacle_ack_sync<=0;obstacle_rejected<=0;
    end else begin
        obstacle_ack_meta<=obstacle_ack;obstacle_ack_sync<=obstacle_ack_meta;
        if(wr_en) begin
            if(wr_sel>=6'h2C && wr_sel<=6'h3B) begin
                for(ob_byte=0;ob_byte<4;ob_byte=ob_byte+1)
                    if(s_axi_wstrb[ob_byte]) obstacle_staging_boxes[(wr_sel-6'h2C)*32+ob_byte*8+:8]<=s_axi_wdata[ob_byte*8+:8];
            end
            case(wr_sel)
                6'h2B: if(&s_axi_wstrb) begin
                    if(!obstacle_busy) begin
                        obstacle_boxes<=obstacle_staging_boxes;
                        obstacle_control<=s_axi_wdata & 32'h03FF030F;
                        obstacle_source<=obstacle_staging_source;
                        obstacle_sequence<=obstacle_staging_sequence;
                        obstacle_req<=~obstacle_req;
                    end else obstacle_rejected<=1;
                end
                6'h3C: if(s_axi_wstrb[0] && s_axi_wdata[2]) obstacle_rejected<=0;
                6'h3E: for(ob_byte=0;ob_byte<4;ob_byte=ob_byte+1)
                    if(s_axi_wstrb[ob_byte]) obstacle_staging_source[ob_byte*8+:8]<=s_axi_wdata[ob_byte*8+:8];
                6'h3F: for(ob_byte=0;ob_byte<4;ob_byte=ob_byte+1)
                    if(s_axi_wstrb[ob_byte]) obstacle_staging_sequence[ob_byte*8+:8]<=s_axi_wdata[ob_byte*8+:8];
                default: ;
            endcase
        end
    end
end

always @(posedge clk) begin
    if (!rst_n) begin
        seg_bright  <= 8'd0;
        phys_blank  <= 9'b1_1111_1111;  // all digits off until PS writes after BTN0
        phys_hex[0] <= 4'h0;
        phys_hex[1] <= 4'h0;
        phys_hex[2] <= 4'h0;
        phys_hex[3] <= 4'h0;
        phys_hex[4] <= 4'h0;
        phys_hex[5] <= 4'h0;
        phys_hex[6] <= 4'h0;
        phys_hex[7] <= 4'h0;
        phys_hex[8] <= 4'h0;
        buz_freq    <= 16'd1000;
        buz_ms      <= 16'd2000;
        ws_mode     <= 3'd0;
        i2c_oe      <= 2'b00;
        ws_color    <= 24'h00FF00;      // GRB red
        ws_bright   <= 8'd40;
        buz_start   <= 1'b0;
        ws_start    <= 1'b0;
        btn_sticky  <= 4'd0;
        key_sticky  <= 5'd0;
        enc_clr_cw  <= 1'b0;
        enc_clr_ccw <= 1'b0;
        enc_clr_pb  <= 1'b0;
        lcd_gpio    <= 6'b011101;         // cs=1,rst=1,blk=1 idle high
        tp_rst_r    <= 1'b1;              // touch out of reset
        debug_led_enable <= 1'b0;
        analysis_read_addr <= 0;
        traffic_display <= 0;
        camera_control <= 32'd12; // measured C2 baseline, polarity profile1 (HREF0x20)
    end else begin
        buz_start   <= 1'b0;
        ws_start    <= 1'b0;
        enc_clr_cw  <= 1'b0;
        enc_clr_ccw <= 1'b0;
        enc_clr_pb  <= 1'b0;

        btn_sticky <= btn_sticky | btn_press;
        key_sticky <= key_sticky | key_press;

        if (wr_en) begin
            case (wr_sel)
                6'h01: begin                                    // 0x04 STATUS_CLR
                    btn_sticky <= btn_sticky & ~s_axi_wdata[7:4];
                end
                6'h02: seg_bright <= s_axi_wdata[7:0];          // 0x08
                6'h03: phys_blank <= s_axi_wdata[8:0];          // 0x0C
                6'h04: begin                                    // 0x10
                    phys_hex[0] <= s_axi_wdata[3:0];
                    phys_hex[1] <= s_axi_wdata[7:4];
                    phys_hex[2] <= s_axi_wdata[11:8];
                    phys_hex[3] <= s_axi_wdata[15:12];
                    phys_hex[4] <= s_axi_wdata[19:16];
                    phys_hex[5] <= s_axi_wdata[23:20];
                    phys_hex[6] <= s_axi_wdata[27:24];
                    phys_hex[7] <= s_axi_wdata[31:28];
                end
                6'h05: phys_hex[8] <= s_axi_wdata[3:0];         // 0x14
                6'h06: buz_start   <= 1'b1;                     // 0x18
                6'h07: buz_freq    <= s_axi_wdata[15:0];        // 0x1C
                6'h08: buz_ms      <= s_axi_wdata[15:0];        // 0x20
                6'h09: begin                                    // 0x24
                    ws_mode  <= s_axi_wdata[2:0];
                    ws_start <= 1'b1;
                end
                6'h0A: ws_color  <= s_axi_wdata[23:0];          // 0x28
                6'h0B: ws_bright <= s_axi_wdata[7:0];           // 0x2C
                6'h0C: lcd_gpio   <= s_axi_wdata[5:0];          // 0x30
                6'h10: i2c_oe     <= s_axi_wdata[1:0];          // 0x40
                6'h12: tp_rst_r   <= s_axi_wdata[0];            // 0x48
                6'h15: debug_led_enable <= s_axi_wdata[0];      // 0x54
                6'h1E: begin                                  // 0x78 CAMERA_CONTROL
                    if (s_axi_wstrb[0]) camera_control[7:0]<=s_axi_wdata[7:0];
                    if (s_axi_wstrb[1]) camera_control[15:8]<=s_axi_wdata[15:8];
                    if (s_axi_wstrb[2]) camera_control[23:16]<=s_axi_wdata[23:16];
                    if (s_axi_wstrb[3]) camera_control[31:24]<=s_axi_wdata[31:24];
                end
                6'h26: begin // 0x98 analysis 32-bit word index (0..38399)
                    if (&s_axi_wstrb[1:0]) analysis_read_addr <= s_axi_wdata[15:0];
                end
                6'h28: begin // 0xA0 coherent traffic display word, atomic full write
                    if (&s_axi_wstrb) traffic_display <= s_axi_wdata;
                end
                6'h0F: begin                                    // 0x3C INPUT_CLR
                    key_sticky  <= key_sticky & ~s_axi_wdata[4:0];
                    enc_clr_cw  <= s_axi_wdata[8];
                    enc_clr_ccw <= s_axi_wdata[9];
                    enc_clr_pb  <= s_axi_wdata[10];
                end
                default: ;
            endcase
        end
    end
end

always @(posedge clk) begin
    if (!rst_n)
        rdata_r <= 32'd0;
    else if (rd_en) begin
        case (rd_sel)
            6'h00: rdata_r <= {22'd0, ws_busy, buz_busy, btn_sticky, btn_level};
            6'h02: rdata_r <= {24'd0, seg_bright};
            6'h03: rdata_r <= {23'd0, phys_blank};
            6'h04: rdata_r <= {phys_hex[7], phys_hex[6], phys_hex[5], phys_hex[4],
                               phys_hex[3], phys_hex[2], phys_hex[1], phys_hex[0]};
            6'h05: rdata_r <= {28'd0, phys_hex[8]};
            6'h07: rdata_r <= {16'd0, buz_freq};
            6'h08: rdata_r <= {16'd0, buz_ms};
            6'h0A: rdata_r <= {8'd0, ws_color};
            6'h0B: rdata_r <= {24'd0, ws_bright};
            6'h0C: rdata_r <= {26'd0, lcd_gpio};
            6'h0D: rdata_r <= {19'd0, key_sticky, 3'd0, key_level}; // 0x34
            6'h0E: rdata_r <= {21'd0, enc_pb_sticky, enc_ccw_sticky, enc_cw_sticky,
                               5'd0, enc_pb_s, enc_b_s, enc_a_s};   // 0x38
            6'h10: rdata_r <= {22'd0, i2c_oe, 6'd0, i2c_sda_i, i2c_scl_i}; // 0x40
            6'h11: rdata_r <= {8'd0, mic_amp, mic_db};              // 0x44
            6'h12: rdata_r <= {23'd0, tp_int, 7'd0, tp_rst_r};      // 0x48
            6'h14: rdata_r <= {16'h4C53, lcd_tx_free, 6'd0,
                               !lcd_tx_ready, lcd_tx_busy};       // 0x50
            6'h15: rdata_r <= {31'd0, debug_led_enable};           // 0x54
            6'h16: rdata_r <= camera_diag0; // 0x58 CAMERA_DIAG0, read only
            6'h17: rdata_r <= camera_diag1; // 0x5C CAMERA_DIAG1, read only
            6'h18: rdata_r <= camera_diag2; // 0x60 PCLK 1ms / HREF total
            6'h19: rdata_r <= camera_diag3; // 0x64 VSYNC edges / capture frames
            6'h1A: rdata_r <= camera_diag4; // 0x68 capture pixel total
            6'h1B: rdata_r <= camera_diag5; // 0x6C previous frame / raw levels
            6'h1C: rdata_r <= camera_diag6; // 0x70 frame lines / pixel snapshot
            6'h1D: rdata_r <= camera_diag7; // 0x74 reject state / first bad line
            6'h1E: rdata_r <= camera_control; // 0x78 camera control
            6'h1F: rdata_r <= jpeg_status; // 0x7C JPEG status
            6'h20: rdata_r <= jpeg_length; // 0x80 exact JPEG byte count
            6'h21: rdata_r <= jpeg_fifo_data; // 0x84 FWFT word, pop on AR acceptance
            6'h22: rdata_r <= jpeg_fifo_level; // 0x88 available 32-bit words
            6'h23: rdata_r <= camera_ddr_status; // 0x8C full-colour DDR capture/read health
            6'h24: rdata_r <= 0; // 0x90 one-shot capture command
            6'h25: rdata_r <= analysis_status; // 0x94 frozen snapshot status/generation
            6'h26: rdata_r <= {16'd0,analysis_read_addr}; // 0x98 index
            6'h27: rdata_r <= analysis_read_data; // 0x9C stable sync BRAM word
            6'h28: rdata_r <= traffic_display; // 0xA0 display/result heartbeat
            6'h2A: rdata_r <= sccb_read_status; // 0xA8 read-only runtime SCCB response
            6'h29: rdata_r <= 32'h54563035; // 0xA4 'TV05' feature identity retained
            6'h2B: rdata_r <= obstacle_control;
            6'h3C: rdata_r <= {obstacle_sequence[15:0],7'd0,obstacle_control[8],obstacle_control[3:0],1'b0,obstacle_rejected,obstacle_ack_sync,obstacle_busy};
            6'h3D: rdata_r <= 32'h4F423032; // OB02 label metadata; retains TV05 compatibility
            6'h3E: rdata_r <= obstacle_staging_source;
            6'h3F: rdata_r <= obstacle_staging_sequence;
            default: if(rd_sel>=6'h2C && rd_sel<=6'h3B) rdata_r <= obstacle_staging_boxes[(rd_sel-6'h2C)*32+:32]; else rdata_r <= 32'd0;
        endcase
    end
end

//-----------------------------------------------------------------------------
// Button debounce (32 ms saturating)
//-----------------------------------------------------------------------------
localparam DB_MAX = 22'd3_200_000;     // 32 ms @ 100 MHz

reg [21:0] db_cnt0, db_cnt1, db_cnt2, db_cnt3;
reg [3:0]  btn_stable, btn_stable_d;

always @(posedge clk) begin
    if (!rst_n) begin
        db_cnt0 <= 22'd0; db_cnt1 <= 22'd0;
        db_cnt2 <= 22'd0; db_cnt3 <= 22'd0;
    end else begin
        if (btn[0]) db_cnt0 <= (db_cnt0 == DB_MAX) ? DB_MAX : db_cnt0 + 1'b1;
        else        db_cnt0 <= (db_cnt0 == 0)      ? 22'd0  : db_cnt0 - 1'b1;
        if (btn[1]) db_cnt1 <= (db_cnt1 == DB_MAX) ? DB_MAX : db_cnt1 + 1'b1;
        else        db_cnt1 <= (db_cnt1 == 0)      ? 22'd0  : db_cnt1 - 1'b1;
        if (btn[2]) db_cnt2 <= (db_cnt2 == DB_MAX) ? DB_MAX : db_cnt2 + 1'b1;
        else        db_cnt2 <= (db_cnt2 == 0)      ? 22'd0  : db_cnt2 - 1'b1;
        if (btn[3]) db_cnt3 <= (db_cnt3 == DB_MAX) ? DB_MAX : db_cnt3 + 1'b1;
        else        db_cnt3 <= (db_cnt3 == 0)      ? 22'd0  : db_cnt3 - 1'b1;
    end
end

always @(posedge clk) begin
    if (!rst_n)
        btn_stable <= 4'd0;
    else begin
        btn_stable[0] <= (db_cnt0 > (DB_MAX >> 1));
        btn_stable[1] <= (db_cnt1 > (DB_MAX >> 1));
        btn_stable[2] <= (db_cnt2 > (DB_MAX >> 1));
        btn_stable[3] <= (db_cnt3 > (DB_MAX >> 1));
    end
end

always @(posedge clk) begin
    if (!rst_n)
        btn_stable_d <= 4'd0;
    else
        btn_stable_d <= btn_stable;
end

assign btn_press = btn_stable & ~btn_stable_d;
assign btn_level = btn_stable;

//-----------------------------------------------------------------------------
// Physical left->right to hardware DIG mapping
// L->R: DIG9, DIG2, DIG1, DIG4, DIG3, DIG6, DIG5, DIG8, DIG7
//-----------------------------------------------------------------------------
wire [3:0] d_r3 = phys_hex[0];
wire [3:0] d_l1 = phys_hex[1];
wire [3:0] d_l0 = phys_hex[2];
wire [3:0] d_l3 = phys_hex[3];
wire [3:0] d_l2 = phys_hex[4];
wire [3:0] d_r0 = phys_hex[5];
wire [3:0] d_mid= phys_hex[6];
wire [3:0] d_r2 = phys_hex[7];
wire [3:0] d_r1 = phys_hex[8];

wire [8:0] blank_hw;
assign blank_hw[8] = phys_blank[0];   // DIG9
assign blank_hw[1] = phys_blank[1];   // DIG2
assign blank_hw[0] = phys_blank[2];   // DIG1
assign blank_hw[3] = phys_blank[3];   // DIG4
assign blank_hw[2] = phys_blank[4];   // DIG3
assign blank_hw[5] = phys_blank[5];   // DIG6
assign blank_hw[4] = phys_blank[6];   // DIG5
assign blank_hw[7] = phys_blank[7];   // DIG8
assign blank_hw[6] = phys_blank[8];   // DIG7

wire [7:0] seg_raw;
wire [8:0] dig_raw;

seg7_driver u_seg7 (
    .clk     (clk),
    .rst_n   (rst_n),
    .dig_l0  (d_l0),
    .dig_l1  (d_l1),
    .dig_l2  (d_l2),
    .dig_l3  (d_l3),
    .dig_mid (d_mid),
    .dig_r0  (d_r0),
    .dig_r1  (d_r1),
    .dig_r2  (d_r2),
    .dig_r3  (d_r3),
    .dp_en   (9'b0),
    .blank   (blank_hw),
    .seg     (seg_raw),
    .dig     (dig_raw)
);

reg [7:0] pwm_cnt;
reg [7:0] pwm_thresh;
always @(posedge clk) begin
    if (!rst_n) begin
        pwm_cnt    <= 8'd0;
        pwm_thresh <= 8'd0;
    end else begin
        pwm_cnt <= pwm_cnt + 1'b1;
        /* (255*bright)/100 ? (bright*652)>>8 ; avoid combo divider */
        if (seg_bright >= 8'd100)
            pwm_thresh <= 8'd255;
        else
            pwm_thresh <= (seg_bright * 16'd652) >> 8;
    end
end

wire pwm_on = (pwm_cnt < pwm_thresh);

assign seg = pwm_on ? seg_raw : 8'hFF;
assign dig = pwm_on ? dig_raw : 9'h0;

//-----------------------------------------------------------------------------
// Buzzer / WS2812
//-----------------------------------------------------------------------------
buzzer_ctrl #(.CLK_HZ(CLK_HZ)) u_buz (
    .clk        (clk),
    .rst_n      (rst_n),
    .start      (buz_start),
    .freq_hz    (buz_freq),
    .dur_ms     (buz_ms),
    .busy       (buz_busy),
    .buzzer_out (buzzer)
);

ws2812_ctrl #(.NUM_LEDS(9), .CLK_HZ(CLK_HZ)) u_ws (
    .clk        (clk),
    .rst_n      (rst_n),
    .start      (ws_start),
    .mode       (ws_mode),
    .color_grb  (ws_color),
    .bright     (ws_bright),
    .busy       (ws_busy),
    .led_din    (ws2812_din)
);

/* Drive-low / release. Pad IOBUF is in rtl/vehicle_top.v (global, not OOC). */
assign i2c_scl_t = ~i2c_oe[0];
assign i2c_sda_t = ~i2c_oe[1];

pdm_level #(.CLK_HZ(CLK_HZ)) u_pdm (
    .clk    (clk),
    .rst_n  (rst_n),
    .mclk   (dmic_mclk),
    .mdat   (dmic_mdat),
    .amp    (mic_amp),
    .db     (mic_db)
);

//-----------------------------------------------------------------------------
// Debug LEDs
//-----------------------------------------------------------------------------
reg [24:0] hb;
always @(posedge clk) begin
    if (!rst_n)
        hb <= 25'd0;
    else
        hb <= hb + 1'b1;
end

assign led[0] = debug_led_enable && btn_level[0];
assign led[1] = debug_led_enable && buz_busy;
assign led[2] = debug_led_enable && ws_busy;
assign led[3] = debug_led_enable && hb[24];

//-----------------------------------------------------------------------------
// K1 five-way + K2 encoder (inputs active-low when pressed / closed)
// Order of key_raw_nswec / key_stable: N, S, W, E, C
//-----------------------------------------------------------------------------
wire [4:0] key_raw_nswec = {~key_c, ~key_e, ~key_w, ~key_s, ~key_n};

reg [21:0] kdb0, kdb1, kdb2, kdb3, kdb4;
reg [4:0]  key_stable, key_stable_d;

always @(posedge clk) begin
    if (!rst_n) begin
        kdb0 <= 22'd0; kdb1 <= 22'd0; kdb2 <= 22'd0;
        kdb3 <= 22'd0; kdb4 <= 22'd0;
    end else begin
        if (key_raw_nswec[0]) kdb0 <= (kdb0 == DB_MAX) ? DB_MAX : kdb0 + 1'b1;
        else                  kdb0 <= (kdb0 == 0)      ? 22'd0  : kdb0 - 1'b1;
        if (key_raw_nswec[1]) kdb1 <= (kdb1 == DB_MAX) ? DB_MAX : kdb1 + 1'b1;
        else                  kdb1 <= (kdb1 == 0)      ? 22'd0  : kdb1 - 1'b1;
        if (key_raw_nswec[2]) kdb2 <= (kdb2 == DB_MAX) ? DB_MAX : kdb2 + 1'b1;
        else                  kdb2 <= (kdb2 == 0)      ? 22'd0  : kdb2 - 1'b1;
        if (key_raw_nswec[3]) kdb3 <= (kdb3 == DB_MAX) ? DB_MAX : kdb3 + 1'b1;
        else                  kdb3 <= (kdb3 == 0)      ? 22'd0  : kdb3 - 1'b1;
        if (key_raw_nswec[4]) kdb4 <= (kdb4 == DB_MAX) ? DB_MAX : kdb4 + 1'b1;
        else                  kdb4 <= (kdb4 == 0)      ? 22'd0  : kdb4 - 1'b1;
    end
end

always @(posedge clk) begin
    if (!rst_n)
        key_stable <= 5'd0;
    else begin
        key_stable[0] <= (kdb0 > (DB_MAX >> 1));
        key_stable[1] <= (kdb1 > (DB_MAX >> 1));
        key_stable[2] <= (kdb2 > (DB_MAX >> 1));
        key_stable[3] <= (kdb3 > (DB_MAX >> 1));
        key_stable[4] <= (kdb4 > (DB_MAX >> 1));
    end
end

always @(posedge clk) begin
    if (!rst_n)
        key_stable_d <= 5'd0;
    else
        key_stable_d <= key_stable;
end

assign key_level = key_stable;
assign key_press = key_stable & ~key_stable_d;

/* Encoder: debounce, CW/CCW on A rising */
reg [15:0] edb_a, edb_b, edb_pb;
reg        ea_s, eb_s, epb_s, ea_d, epb_d;

always @(posedge clk) begin
    if (!rst_n) begin
        edb_a <= 16'd0; edb_b <= 16'd0; edb_pb <= 16'd0;
    end else begin
        if (!enc_a) edb_a <= (edb_a == 16'hFFFF) ? 16'hFFFF : edb_a + 1'b1;
        else        edb_a <= (edb_a == 0) ? 16'd0 : edb_a - 1'b1;
        if (!enc_b) edb_b <= (edb_b == 16'hFFFF) ? 16'hFFFF : edb_b + 1'b1;
        else        edb_b <= (edb_b == 0) ? 16'd0 : edb_b - 1'b1;
        if (!enc_pb) edb_pb <= (edb_pb == 16'hFFFF) ? 16'hFFFF : edb_pb + 1'b1;
        else         edb_pb <= (edb_pb == 0) ? 16'd0 : edb_pb - 1'b1;
    end
end

always @(posedge clk) begin
    if (!rst_n) begin
        ea_s <= 1'b0; eb_s <= 1'b0; epb_s <= 1'b0;
        ea_d <= 1'b0; epb_d <= 1'b0;
        enc_cw_sticky  <= 1'b0;
        enc_ccw_sticky <= 1'b0;
        enc_pb_sticky  <= 1'b0;
    end else begin
        ea_s  <= (edb_a > 16'd1000);
        eb_s  <= (edb_b > 16'd1000);
        epb_s <= (edb_pb > 16'd1000);
        ea_d  <= ea_s;
        epb_d <= epb_s;

        if (enc_clr_cw)
            enc_cw_sticky <= 1'b0;
        else if (ea_s & ~ea_d & ~eb_s)
            enc_cw_sticky <= 1'b1;

        if (enc_clr_ccw)
            enc_ccw_sticky <= 1'b0;
        else if (ea_s & ~ea_d & eb_s)
            enc_ccw_sticky <= 1'b1;

        if (enc_clr_pb)
            enc_pb_sticky <= 1'b0;
        else if (epb_s & ~epb_d)
            enc_pb_sticky <= 1'b1;
    end
end

assign enc_a_s  = ea_s;
assign enc_b_s  = eb_s;
assign enc_pb_s = epb_s;

//-----------------------------------------------------------------------------
// LCD GPIO (bit-bang SPI from PS)
// bit0 sck, bit1 mosi, bit2 dc, bit3 cs, bit4 rst, bit5 blk  (active high)
//-----------------------------------------------------------------------------
/* GPIO commands remain compatible with the previous bitstream. Software must
 * set GPIO SCK=0 before enqueuing pixels, and wait for LCD_TX_STAT.busy=0
 * before changing CS/DC or issuing GPIO commands. The serializer relinquishes
 * the pins only after the last falling edge, with SCK already low. */
lcd_spi_tx #(.FIFO_DEPTH(64), .HALF_PERIOD_CYCLES(4)) u_lcd_tx (
    .clk(clk), .rst_n(rst_n),
    .push(wr_en && wr_sel == 6'h13 && (&s_axi_wstrb[1:0])),
    .pixel(s_axi_wdata[15:0]), .ready(lcd_tx_ready),
    .free_slots(lcd_tx_free), .busy(lcd_tx_busy),
    .sck(lcd_tx_sck), .mosi(lcd_tx_mosi)
);

/* Preserve the advertised R9 hardware SPI FIFO path. */
assign lcd_sck  = lcd_tx_busy ? lcd_tx_sck : lcd_gpio[0];
assign lcd_mosi = lcd_tx_busy ? lcd_tx_mosi : lcd_gpio[1];
assign lcd_dc   = lcd_gpio[2];
assign lcd_cs   = lcd_gpio[3];
assign lcd_rst  = lcd_gpio[4];
assign lcd_blk  = lcd_gpio[5];
assign tp_rst   = tp_rst_r;

assign pl_clk  = clk;
assign pl_rstn = rst_n;

endmodule
