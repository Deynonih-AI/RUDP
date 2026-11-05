`timescale 1ns / 1ps

module psram_syn_top (
    input logic clk_100m,
    input logic clk_25m,
    input logic rst_n,
    input logic soft_rst,
    input logic speedis1000_i,

    // --- PSRAM Physical Pins (2 Channels) ---
    output logic [ 1:0] O_psram_ck,
    output logic [ 1:0] O_psram_ck_n,
    output logic [ 1:0] O_psram_cs_n,
    output logic [ 1:0] O_psram_reset_n,
    inout  wire  [15:0] IO_psram_dq,
    inout  wire  [ 1:0] IO_psram_rwds,

    // --- MAC Interface ---
    input  logic [ 7:0] udp_dout,
    input  logic        udp_vout,
    output logic [15:0] udp_len,
    output logic [ 7:0] udp_din,
    output logic        udp_vin,
    input  logic        udp_cts,

    // --- Status ---
    output logic init_done,
    output logic transaction_busy,
    output logic timeout_event
);

  // ========================================================
  //  PLL
  // ========================================================
  logic memory_clk;
  logic memory_clk_p;
  logic pll_lock;
  logic clk_out;

  gowin_rpll_psram u_pll (
      .clkout(memory_clk),
      .lock(pll_lock),
      .clkoutp(memory_clk_p),
      .clkoutd(),
      .reset(~rst_n),
      .clkin(clk_100m)
  );

  // ========================================================
  //  IP Interface (Channel 0 Only)
  // ========================================================
  logic psram_cmd, psram_cmd_en, psram_cmd_rdy;
  logic [20:0] psram_addr_from_core;
  logic [20:0] addr_ch0;
  logic [31:0] psram_wr_data;
  logic [ 3:0] psram_mask;
  logic [31:0] psram_rd_data;
  logic        psram_rd_valid;
  logic        init_calib;

  // Сдвиг адреса для канала 0 (младший бит IP использует для выбора канала)
  assign addr_ch0 = {psram_addr_from_core[19:0], 1'b0};

  // ========================================================
  //  PSRAM IP (2CH, Ch0 used, Ch1 disabled)
  // ========================================================
  PSRAM_Memory_Interface_HS_Top u_psram (
      .clk(clk_100m),
      .rst_n(rst_n),
      .memory_clk(memory_clk),
      .pll_lock(pll_lock),

      // Physical Pins
      .O_psram_ck(O_psram_ck),
      .O_psram_ck_n(O_psram_ck_n),
      .O_psram_cs_n(O_psram_cs_n),
      .O_psram_reset_n(O_psram_reset_n),
      .IO_psram_dq(IO_psram_dq),
      .IO_psram_rwds(IO_psram_rwds),
      .clk_out(clk_out),

      // --- Channel 0 (Active) ---
      .init_calib0   (init_calib),
      .cmd0          (psram_cmd),
      .cmd_en0       (psram_cmd_en),
      .cmd_rdy0      (psram_cmd_rdy),
      .addr0         (addr_ch0),
      .wr_data0      (psram_wr_data),
      .data_mask0    (psram_mask),
      .rd_data0      (psram_rd_data),
      .rd_data_valid0(psram_rd_valid),

      // --- Channel 1 (Tie-off) ---
      .init_calib1(),
      .cmd1(1'b0),
      .cmd_en1(1'b0),
      .cmd_rdy1(),
      .addr1(21'd0),
      .wr_data1(32'd0),
      .data_mask1(4'd0),
      .rd_data1(),
      .rd_data_valid1()
  );

  assign init_done = init_calib;

  // ========================================================
  //  RUDP Core
  // ========================================================
  rudp_core #(
      // 15 сек при clk_psram = 50 МГц.
      // Худший случай из тестов (32% потерь на 4096 блоков) ~10 сек.
      // Запас 50%. Если PC реально умер — FPGA 15 сек висит в
      // ST_WAIT_COMPLETE, потом сбрасывается в ST_IDLE.
      .TIMEOUT_CYCLES(1_500_000_000)
  ) u_rudp (
      .clk(clk_25m),
      .clk_psram(clk_out),
      .rst(~rst_n),
      .soft_rst(soft_rst),
      .speedis1000_i(speedis1000_i),
      .udp_dout(udp_dout),
      .udp_vout(udp_vout),
      .udp_len(udp_len),
      .udp_din(udp_din),
      .udp_vin(udp_vin),
      .udp_cts(udp_cts),

      .psram_cmd(psram_cmd),
      .psram_cmd_en(psram_cmd_en),
      .psram_addr(psram_addr_from_core),
      .psram_wr_data(psram_wr_data),
      .psram_mask(psram_mask),
      .psram_cmd_rdy(psram_cmd_rdy),
      .psram_rd_data(psram_rd_data),
      .psram_rd_valid(psram_rd_valid),
      .init_calib0(init_calib),
      .transaction_busy(transaction_busy),
      .state_dbg(),
      .timeout_counter_dbg(),
      .timeout_event(timeout_event)
  );

endmodule
