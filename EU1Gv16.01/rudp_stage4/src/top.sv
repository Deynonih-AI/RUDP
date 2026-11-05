//=============================================================================
//  top.sv
//
//  Gowin Ethernet development kit — top-level wrapper.
//
//  Architecture:
//
//            __________________________________________________________________________
//           |                                                                          |
//           |      ________       ________        ______________          ________     |
//    _____  |     |        |     |        |      |              |        |        |    |
//   | PHY | |     |  MAC   |     | eth_   |      | psram_syn_   |        | Hyper  |    |
//   | IC  |<----->| (TSEM) |<--->| vlg    |<---->| top          |<--HB-->| RAM IC |    |
//   |_____| |     |        |GMII | (UDP+  | UDP  | (RUDP + PSRAM| Hyper  |        |    |
//    RGMII  |     |________|     |  DHCP) |      |  controller) |  Bus   |________|    |
//    MDIO   |        ^                           |______________|                       |
//           |        | MDIO mgmt (speed / duplex / link)                               |
//           |     ___|______                                                           |
//           |    | phy_      |                                                         |
//           |    | status_   |                                                         |
//           |    | reader    |                                                         |
//           |    |___________|                                                         |
//           |__________________________________________________________________________|
//
//  Notes:
//    - eth_vlg provides only the UDP/DHCP transport; TCP is intentionally absent.
//    - RUDP is implemented inside psram_syn_top, on top of the UDP byte stream.
//    - MDIO is shared between the MAC hard-block and phy_status_reader.
//
//=============================================================================

module top
  import eth_vlg_pkg::*;
(
    //-----------------------------------------------------------------------
    // RGMII / Ethernet PHY
    //-----------------------------------------------------------------------
    input logic       rgmii_rx_clk,
    input logic       rgmii_rx_ctl,
    input logic [3:0] rgmii_rx_dat,

    output logic       rgmii_gtx_clk,
    output logic       rgmii_tx_ctl,
    output logic [3:0] rgmii_tx_dat,

    output logic mdc,
    inout  wire  mdio,

    //-----------------------------------------------------------------------
    // Debug I/O
    //-----------------------------------------------------------------------
    (* keep *) output logic       gpio0,  // reserved
    (* keep *) output logic       gpio1,
    (* keep *) output logic       gpio2,
    (* keep *) output logic [2:0] led,

    //-----------------------------------------------------------------------
    // Clocks
    //-----------------------------------------------------------------------
    input  logic gen_clk_100m,
    output logic ckxtail1_fpga,

    //-----------------------------------------------------------------------
    // PHY reset
    //-----------------------------------------------------------------------
    output logic phyrstb,

    //-----------------------------------------------------------------------
    // HyperRAM / PSRAM
    //-----------------------------------------------------------------------
    output [ 1:0] O_psram_ck,
    output [ 1:0] O_psram_ck_n,
    inout  [ 1:0] IO_psram_rwds,
    output [ 1:0] O_psram_reset_n,
    inout  [15:0] IO_psram_dq,
    output [ 1:0] O_psram_cs_n
);

  import rudp_types::*;

  //==========================================================================
  // Parameters
  //==========================================================================
  parameter [3:0][7:0] PREFERRED_IPV4 = {8'd192, 8'd168, 8'd10, 8'd213};

  localparam int REFCLK_HZ = 25_000_000;

  //==========================================================================
  // Reset
  // No external reset pin on this board — tie active-high reset low.
  //==========================================================================
  logic arst;
  assign arst = 1'b0;

  logic rst_125m;
  logic rst_25m;
  assign rst_125m = arst;
  assign rst_25m  = arst;

  // Soft reset — no active source in this design; held inactive.
  logic softreset;
  assign softreset = 1'b0;

  //==========================================================================
  // Clocks
  //==========================================================================
  logic clk_125m;
  logic clk_25m;

  gw_pll_100_25 gw_pll_100_25_inst (
      .clkout(clk_25m),
      .reset (arst),
      .clkin (gen_clk_100m)
  );

  gowin_rpll_100_125 gowin_rpll_100_125_inst (
      .clkout(clk_125m),
      .reset (arst),
      .clkin (gen_clk_100m)
  );

  assign ckxtail1_fpga = clk_25m;
  assign phyrstb       = !arst;

  //==========================================================================
  // GMII signals between the MAC hard-block and eth_vlg stack
  //==========================================================================
  logic [ 7:0] phy_rx_dat;
  logic        phy_rx_val;
  logic        phy_rx_err;
  logic        phy_rx_clk;

  logic [ 7:0] phy_tx_dat;
  logic        phy_tx_val;
  logic        phy_tx_err;
  logic        phy_tx_clk;  // MAC TX clock output (kept for timing constraints)

  logic        phy_tx_ready;
  logic        phy_tx_last;
  logic        rx_mac_last_o;

  //==========================================================================
  // MAC <-> PHY management (MDIO)
  //==========================================================================
  logic        speedis1000_i;
  logic        speedis10_i;
  logic        duplex_status_i;
  logic        link_up;

  logic [ 4:0] miim_phyad_i;
  logic [ 4:0] miim_regad_i;
  logic [15:0] miim_wrdata_i;
  logic        miim_wren_i;
  logic        miim_rden_i;
  logic [15:0] miim_rddata_o;
  logic        miim_rddata_valid_o;
  logic        miim_busy_o;

  logic        mdio_in_i;
  logic        mdio_out_o;
  logic        mdio_oen_o;

  assign mdio_in_i = mdio;
  assign mdio      = (!mdio_oen_o) ? mdio_out_o : 1'bz;

  //==========================================================================
  // UDP / DHCP interface to eth_vlg
  // (RUDP framing lives inside psram_syn_top, not here)
  //==========================================================================
  logic  [15:0] udp_len;
  port_t        udp_loc_port;
  ipv4_t        udp_ipv4_rx;
  port_t        udp_rem_port_rx;
  ipv4_t        udp_ipv4_tx;
  port_t        udp_rem_port_tx;

  logic         ready;
  logic         error;

  ipv4_t        preferred_ipv4;
  ipv4_t        assigned_ipv4;
  logic         dhcp_lease;
  logic         dhcp_start;

  logic         udp_cts;
  logic         udp_vout;
  logic  [ 7:0] udp_dout;
  logic  [ 7:0] udp_din;
  logic         udp_vin;

  assign preferred_ipv4  = PREFERRED_IPV4;
  assign udp_loc_port    = 1234;
  assign udp_ipv4_tx     = {8'd192, 8'd168, 8'd100, 8'd2};
  assign udp_rem_port_tx = 1233;

  //==========================================================================
  // Gowin Triple-Speed Ethernet MAC
  //==========================================================================
  Triple_Speed_Ethernet_MAC_Top Triple_Speed_Ethernet_MAC_inst (
      // RGMII PHY side
      .rgmii_rxc   (rgmii_rx_clk),
      .rgmii_rx_ctl(rgmii_rx_ctl),
      .rgmii_rxd   (rgmii_rx_dat),

      .gtx_clk     (clk_125m),
      .rgmii_txc   (rgmii_gtx_clk),
      .rgmii_tx_ctl(rgmii_tx_ctl),
      .rgmii_txd   (rgmii_tx_dat),

      .speedis1000  (speedis1000_i),
      .speedis10    (speedis10_i),
      .duplex_status(duplex_status_i),

      .rstn(!arst),

      // RX MAC side (to eth_vlg)
      .rx_mac_clk  (phy_rx_clk),
      .rx_mac_valid(phy_rx_val),
      .rx_mac_data (phy_rx_dat),
      .rx_mac_last (rx_mac_last_o),
      .rx_mac_error(phy_rx_err),

      // RX statistics (unused)
      .rx_statistics_valid (),
      .rx_statistics_vector(),

      // TX MAC side (from eth_vlg)
      .tx_mac_clk  (phy_tx_clk),
      .tx_mac_valid(phy_tx_val),
      .tx_mac_data (phy_tx_dat),
      .tx_mac_last (phy_tx_last),
      .tx_mac_error(phy_tx_err),
      .tx_mac_ready(phy_tx_ready),

      // TX statistics (unused)
      .tx_statistics_valid (),
      .tx_statistics_vector(),

      // Frame processing
      //   rx_fcs_fwd_ena = 1 : keep incoming FCS in the data stream
      //   tx_fcs_fwd_ena = 0 : application must supply a valid FCS
      .rx_fcs_fwd_ena      (1'b1),
      .rx_jumbo_ena        (1'b0),
      .rx_pause_req        (),
      .tx_fcs_fwd_ena      (1'b0),
      .tx_ifg_delay_ena    (1'b0),
      .tx_ifg_delay        ('0),
      .tx_pause_req        (1'b0),
      .tx_pause_val        (16'd0),
      .tx_pause_source_addr(48'h0),

      // MDIO management interface
      .clk              (clk_25m),
      .miim_phyad       (miim_phyad_i),
      .miim_regad       (miim_regad_i),
      .miim_wrdata      (miim_wrdata_i),
      .miim_wren        (miim_wren_i),
      .miim_rden        (miim_rden_i),
      .miim_rddata      (miim_rddata_o),
      .miim_rddata_valid(miim_rddata_valid_o),
      .miim_busy        (miim_busy_o),

      .mdc     (mdc),
      .mdio_in (mdio_in_i),
      .mdio_out(mdio_out_o),
      .mdio_oen(mdio_oen_o)
  );

  //==========================================================================
  // PHY (RTL8211) status reader — link / speed / duplex
  //==========================================================================
  phy_status_reader phy_status_reader_inst (
      .clk(clk_25m),
      .rst(arst),

      // MIIM interface (shared with MAC)
      .miim_read    (miim_rden_i),
      .miim_write   (miim_wren_i),
      .miim_phy_addr(miim_phyad_i),
      .miim_reg_addr(miim_regad_i),
      .miim_wr_data (miim_wrdata_i),
      .miim_rd_data (miim_rddata_o),
      .miim_busy    (miim_busy_o),
      .miim_done    (miim_rddata_valid_o),

      // Outputs to MAC
      .speedis1000(speedis1000_i),
      .speedis10  (speedis10_i),
      .duplex     (duplex_status_i),
      .link_up    (link_up)
  );

  //==========================================================================
  // eth_vlg — UDP + DHCP transport only (no TCP, no RUDP)
  //==========================================================================

  eth_vlg #(
      .MAC_ADDR       ({8'h0C, 8'hAB, 8'hFA, 8'hCE, 8'hBE, 8'hEF}),
      .DEFAULT_GATEWAY({8'd192, 8'd168, 8'd10, 8'd1}),
      .MTU            (1500),

      // ---- mDNS / DHCP identity (variant A) ----
      //   DOMAIN_NAME_LEN = 5  -> "fpga0"
      //   HOSTNAME_LEN    = 8  -> "rudp_dev"
      //   FQDN_LEN        = 9  -> "fpga_rudp"
      .DOMAIN_NAME_LEN(5),
      .HOSTNAME_LEN   (8),
      .FQDN_LEN       (9),
      .DOMAIN_NAME    ("fpga0"),
      .HOSTNAME       ("rudp_dev"),
      .FQDN           ("fpga_rudp"),

      .DHCP_TIMEOUT_SEC(3),
      .DHCP_ENABLE     (1),
      .ARP_VERBOSE     (0),
      .DHCP_VERBOSE    (0),
      .UDP_VERBOSE     (0),
      .IPV4_VERBOSE    (0),
      .MAC_VERBOSE     (0)
  ) eth_vlg_inst (
      .clk125m  (clk_125m),
      .rst125m  (rst_125m | (!link_up)),
      .clk25m   (clk_25m),
      .rst25m   (rst_25m  | (!link_up)),
      .softreset(softreset),

      // PHY RX
      .phy_rx_clk (phy_rx_clk),
      .phy_rx_err (phy_rx_err),
      .phy_rx_val (phy_rx_val),
      .phy_rx_dat (phy_rx_dat),
      .phy_rx_last(rx_mac_last_o),

      // PHY TX
      .phy_tx_err  (phy_tx_err),
      .phy_tx_val  (phy_tx_val),
      .phy_tx_dat  (phy_tx_dat),
      .phy_tx_ready(phy_tx_ready),
      .phy_tx_last (phy_tx_last),

      // UDP
      .udp_len        (udp_len),
      .udp_din        (udp_din),
      .udp_vin        (udp_vin),
      .udp_cts        (udp_cts),
      .udp_dout       (udp_dout),
      .udp_vout       (udp_vout),
      .udp_loc_port   (udp_loc_port),
      .udp_ipv4_rx    (udp_ipv4_rx),
      .udp_rem_port_rx(udp_rem_port_rx),
      .udp_ipv4_tx    (udp_ipv4_tx),
      .udp_rem_port_tx(udp_rem_port_tx),

      // Core status (named, kept for debug)
      .dhcp_ready(ready),
      .dhcp_error(error),

      // DHCP
      .preferred_ipv4(preferred_ipv4),
      .assigned_ipv4 (assigned_ipv4),
      .dhcp_start    (dhcp_start),
      .dhcp_lease    (dhcp_lease),

      // DNS (outputs unused, neutral target name)
      .dns_ipv4_pri({8'd8, 8'd8, 8'd8, 8'd8}),
      .dns_ipv4_sec({8'd8, 8'd8, 8'd4, 8'd4}),
      .dns_name    ("fpga.local"),              // mDNS-reserved .local TLD, device-specific
      .dns_start   (),
      .dns_ipv4    (),
      .dns_ready   (),
      .dns_error   ()
  );

  //==========================================================================
  // DHCP lease request pulse generator — 1 Hz
  //==========================================================================
  logic [$clog2(REFCLK_HZ)-1:0] start_ctr = '0;

  always @(posedge clk_25m) begin
    start_ctr <= (start_ctr == REFCLK_HZ) ? 0 : start_ctr + 1;
  end

  always @(posedge clk_125m) begin
    dhcp_start <= (start_ctr == 0) && !dhcp_lease;
  end

  //==========================================================================
  // psram_syn_top — PSRAM controller + RUDP endpoint logic
  //
  //   - implements RUDP on top of the UDP byte stream from eth_vlg
  //   - performs HyperBus transactions to the external HyperRAM
  //
  //==========================================================================
  logic transaction_busy;
  logic timeout_event;

  psram_syn_top u_top (
      .clk_100m     (gen_clk_100m),
      .clk_25m      (clk_25m),
      .rst_n        (!(rst_25m | (!link_up))),
      .soft_rst     (softreset),
      .speedis1000_i(speedis1000_i),

      .O_psram_ck     (O_psram_ck),
      .O_psram_ck_n   (O_psram_ck_n),
      .O_psram_cs_n   (O_psram_cs_n),
      .O_psram_reset_n(O_psram_reset_n),
      .IO_psram_dq    (IO_psram_dq),
      .IO_psram_rwds  (IO_psram_rwds),

      // UDP stream to/from eth_vlg
      .udp_dout(udp_dout),
      .udp_vout(udp_vout),
      .udp_len (udp_len),
      .udp_din (udp_din),
      .udp_vin (udp_vin),
      .udp_cts (udp_cts),

      // Status
      .init_done       (),
      .transaction_busy(transaction_busy),
      .timeout_event   (timeout_event)
  );

  //==========================================================================
  // Status LEDs (active-low: LED on = '0')
  //==========================================================================
  (* keep *)assign led[0] = !speedis1000_i;  // on = 1 Gb/s link
  (* keep *)assign led[1] = !link_up;  // on = link up
  (* keep *)assign led[2] = 1'b1;  // unused — LED kept off

  //==========================================================================
  // Debug GPIO
  //==========================================================================
  (* keep *)assign gpio1  = timeout_event;
  (* keep *)assign gpio2  = transaction_busy;

  // gpio0 is reserved for future debug use and is intentionally undriven.

endmodule
