`timescale 1ns/1ps
module tb_pc_labels;
reg clk=0;always #5 clk=~clk;
reg rst_n=0,live=1,in_de=1,in_hsync=1,in_vsync=1,req_async=0;
reg [23:0] input_rgb=24'h123456;reg [9:0] in_x=0,in_y=0;
reg [511:0] boxes_async=0;reg [31:0] control_async=0,source_async=0,sequence_async=0;
wire ack;wire [23:0] output_rgb;wire out_de,out_hsync,out_vsync;wire [9:0]out_x,out_y;
camera_obstacle_overlay #(.PIX_CLK_HZ(1000000),.TIMEOUT_CYCLES(2000)) dut(.*);
task pixel(input integer x,y,input [23:0]rgb);begin
 @(negedge clk);in_x=x;in_y=y;@(posedge clk);#1;
 if(output_rgb!==rgb||out_de!==in_de||out_x!==in_x||out_y!==in_y)$fatal(1,"label pixel %d %d got %h expected %h",x,y,output_rgb,rgb);
end endtask
initial begin
 #33;rst_n=1;repeat(20)@(negedge clk);
 boxes_async[31:0]=(50<<16)|10|(17<<10);boxes_async[63:32]=(100<<16)|80;
 control_async=32'h301;source_async=1;sequence_async=1;req_async=1;
 repeat(20)@(negedge clk);in_vsync=0;@(negedge clk);in_vsync=1;repeat(3)@(negedge clk);
 if(ack!==req_async)$fatal(1,"metadata mailbox not committed");
 pixel(10,60,24'h00ff00);pixel(30,60,24'h123456);
 pixel(11,41,24'hffffff);pixel(15,41,24'h006000);
 pixel(35,41,24'hffffff); // final digit 7 top-row column1
 live=0;repeat(2)@(negedge clk);pixel(11,41,24'h123456);
 live=1;repeat(2010)@(negedge clk);pixel(10,60,24'h123456);
 $display("PC_LABEL_MAILBOX_PIXEL_LIVE_TTL_PASS");$finish;
end
endmodule
