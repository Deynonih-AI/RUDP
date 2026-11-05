# ============================================================================
#  Базовые клоки
# ============================================================================
create_clock -name gen_clk_100m -period 10.000 -waveform {0 5} [get_ports {gen_clk_100m}]
create_clock -name rgmii_rx_clk -period 8.000 -waveform {0 4} [get_ports {rgmii_rx_clk}]

# ============================================================================
#  PLL 125 МГц и 25 МГц
# ============================================================================
create_generated_clock -name clk_125m -source [get_ports {gen_clk_100m}] -master_clock gen_clk_100m -multiply_by 5 -divide_by 4 [get_pins {gowin_rpll_100_125_inst/rpll_inst/CLKOUT}]

create_generated_clock -name clk_25m -source [get_ports {gen_clk_100m}] -master_clock gen_clk_100m -divide_by 4 [get_pins {gw_pll_100_25_inst/rpll_inst/CLKOUT}]

# ============================================================================
#  RGMII RX
# ============================================================================
set_input_delay -clock [get_clocks {rgmii_rx_clk}] -max 2.0 [get_ports {rgmii_rx_ctl rgmii_rx_dat[3] rgmii_rx_dat[2] rgmii_rx_dat[1] rgmii_rx_dat[0]}]
set_input_delay -clock [get_clocks {rgmii_rx_clk}] -min 1.0 [get_ports {rgmii_rx_ctl rgmii_rx_dat[3] rgmii_rx_dat[2] rgmii_rx_dat[1] rgmii_rx_dat[0]}]
set_input_delay -clock [get_clocks {rgmii_rx_clk}] -clock_fall -max 2.0 -add_delay [get_ports {rgmii_rx_ctl rgmii_rx_dat[3] rgmii_rx_dat[2] rgmii_rx_dat[1] rgmii_rx_dat[0]}]
set_input_delay -clock [get_clocks {rgmii_rx_clk}] -clock_fall -min 1.0 -add_delay [get_ports {rgmii_rx_ctl rgmii_rx_dat[3] rgmii_rx_dat[2] rgmii_rx_dat[1] rgmii_rx_dat[0]}]

# ============================================================================
#  Асинхронные группы
# ============================================================================
set_clock_groups -asynchronous -group [get_clocks {rgmii_rx_clk}] 
set_clock_groups -asynchronous -group [get_clocks {clk_125m}] 
set_clock_groups -asynchronous -group [get_clocks {clk_25m}] 
set_clock_groups -asynchronous -group [get_clocks {gen_clk_100m}]

# ============================================================================
#  Игнорируем пути калибровки PSRAM (накрываем ОБА канала: top0 и top1)
# ============================================================================
set_false_path -from [get_pins {*u_psram_init/calib_0_s0/Q}]

# ============================================================================
#  Разрешаем 2 такта для пути ready -> FIFO внутри MAC (лечим -0.118 нс)
# ============================================================================
set_multicycle_path 2 -setup -from [get_pins {*/u_mac_tx_ctrl/tx_mac_ready_o_s0/Q}] -to [get_pins {*/cdc_tx/dc_fifo/*/CEB}]
set_multicycle_path 1 -hold  -from [get_pins {*/u_mac_tx_ctrl/tx_mac_ready_o_s0/Q}] -to [get_pins {*/cdc_tx/dc_fifo/*/CEB}]

# ============================================================================
#  GPIO / LED
# ============================================================================
set_false_path -to [get_ports {gpio0 gpio1 gpio2 led[2] led[1] led[0]}]