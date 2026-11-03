`timescale 1ns/1ps
module tb_analysis_antialias;
  reg clk=0;always #5 clk=~clk;
  reg rst_n=0,enabled=1,arm=0,frame_start=0,frame_good=1,pix_valid=0;
  reg [19:0] pixel_addr=0;reg [15:0] pixel_data=0,read_addr=0;
  wire [31:0] data,status,old_data,old_status;
  camera_analysis_buffer #(.WIDTH(8),.HEIGHT(4),.ANTIALIAS(1)) dut(
    clk,rst_n,enabled,arm,frame_start,frame_good,pix_valid,pixel_addr,pixel_data,read_addr,data,status);
  camera_analysis_buffer #(.WIDTH(8),.HEIGHT(4),.ANTIALIAS(0)) legacy(
    clk,rst_n,enabled,arm,frame_start,frame_good,pix_valid,pixel_addr,pixel_data,read_addr,old_data,old_status);
  integer i,k,pattern,checks=0;reg [15:0] expected,source_pixel;
  task start_capture;
    begin
      @(negedge clk);arm=1;@(negedge clk);arm=0;frame_start=1;
      @(negedge clk);frame_start=0;
    end
  endtask
  initial begin
    repeat(3)@(negedge clk);rst_n=1;
    // Constant primary colours exercise independent component carries;
    // checkerboard proves all four neighbours contribute to each result.
    for(pattern=0;pattern<5;pattern=pattern+1)begin
      start_capture;
      for(i=0;i<32;i=i+1)begin
        pixel_addr=i;pix_valid=1;
        case(pattern)
          0:pixel_data=16'hffff;
          1:pixel_data=16'hf800;
          2:pixel_data=16'h07e0;
          3:pixel_data=16'h001f;
          4:pixel_data=((i%8+i/8)%2)?16'hffff:0;
        endcase
        @(negedge clk);
      end
      pix_valid=0;frame_start=1;@(negedge clk);frame_start=0;
      if(status[2:0]!==3'b001 || old_status[2:0]!==3'b001)$fatal(1,"frame failed");
      case(pattern)
        0:expected=16'hffff;1:expected=16'hf800;2:expected=16'h07e0;
        3:expected=16'h001f;4:expected=16'h8410;
      endcase
      for(k=0;k<4;k=k+1)begin
        read_addr=k;@(posedge clk);#1;
        if(data!=={expected,expected})$fatal(1,"filtered mismatch pattern %d word %d got %h",pattern,k,data);
        source_pixel=(pattern==4)?16'h0000:expected;
        if(old_data!=={source_pixel,source_pixel})$fatal(1,"legacy fallback mismatch");
        checks=checks+2;@(negedge clk);
      end
    end
    // Out-of-order/incomplete frame must never publish stale storage.
    start_capture;pix_valid=1;pixel_addr=7;pixel_data=16'hffff;
    @(negedge clk);pix_valid=0;frame_start=1;
    @(negedge clk);frame_start=0;
    if(status[2:0]!==3'b100)$fatal(1,"invalid frame was published");
    checks=checks+1;$display("PASS analysis antialias %d checks",checks);$finish;
  end
  initial begin #100000;$fatal(1,"timeout");end
endmodule
