//Copyright (C)2014-2024 Gowin Semiconductor Corporation.
//All rights reserved.
//File Title: Template file for instantiation
//Tool Version: V1.9.9.02
//Part Number: GW2AR-LV18EQ144PC8/I7
//Device: GW2AR-18
//Device Version: C
//Created Time: Thu Feb 19 11:12:00 2026

//Change the instance name and port connections to the signal names
//--------Copy here to design--------

    gw_pll_tx your_instance_name(
        .clkout(clkout_o), //output clkout
        .lock(lock_o), //output lock
        .clkoutp(clkoutp_o), //output clkoutp
        .reset(reset_i), //input reset
        .clkin(clkin_i), //input clkin
        .psda(psda_i), //input [3:0] psda
        .dutyda(dutyda_i), //input [3:0] dutyda
        .fdly(fdly_i) //input [3:0] fdly
    );

//--------Copy end-------------------
