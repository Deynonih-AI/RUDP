module mac_vlg_cdc_gowin #(
  parameter integer FIFO_DEPTH = 2048
)(
  input  logic       clk_in,
  input  logic       rst_in,
  input  logic [8:0] data_in,
  input  logic       valid_in,
  output logic       ready_in,

  input  logic       clk_out,
  input  logic       rst_out,
  output logic [8:0] data_out,
  output logic       valid_out,
  input  logic       ready_out,

  output logic       Almost_Full_o,
  output logic       debug0,
  output logic       debug1

);
  // We provide back-pressure: producer sees ready_in = ~full

  wire full, empty;
  wire rd_en = ready_out && !empty;
  wire wr_en = valid_in && !full;

  initial begin
   // full = 0;
    //empty = 1;
//    rd_en = 0;
//    wr_en = 0;
  end


  // gowin_dc_fifo #(.DATA_WIDTH(8), .DEPTH(FIFO_DEPTH)) dc_fifo (
  //   .clk_wr(clk_in), .rst_wr(rst_in),
  //   .wr_data(data_in), .wr_en(wr_en), .full(full),
  //   .clk_rd(clk_out), .rst_rd(rst_out),
  //   .rd_data(data_out), .rd_en(rd_en), .empty(empty)
  // );


	gowin_dc_fifo dc_fifo(
		.Data(data_in), //input [7:0] Data
		.WrReset(rst_in), //input WrReset
		.RdReset(rst_out), //input RdReset
		.WrClk(clk_in), //input WrClk
		.RdClk(clk_out), //input RdClk
		.WrEn(wr_en), //input WrEn
		.RdEn(rd_en), //input RdEn
		.Almost_Full(Almost_Full_o), //output Almost_Full
		.Q(data_out), //output [7:0] Q
		.Empty(empty), //output Empty
		.Full(full) //output Full
	);

  assign ready_in  = ~full;
  assign valid_out = ~empty;

  assign debug0 = rd_en;
//  assign debug1 = empty;


endmodule : mac_vlg_cdc_gowin