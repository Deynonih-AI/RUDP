//Copyright (C)2014-2026 Gowin Semiconductor Corporation.
//All rights reserved.
//File Title: Template file for instantiation
//Tool Version: V1.9.12.03 (64-bit)
//IP Version: 1.0
//Part Number: GW2AR-LV18EQ144PC8/I7
//Device: GW2AR-18
//Device Version: C
//Created Time: Thu Oct 29 12:43:32 2026

//Change the instance name and port connections to the signal names
//--------Copy here to design--------

	fifo_rx_hs your_instance_name(
		.Data(Data), //input [8:0] Data
		.Reset(Reset), //input Reset
		.WrClk(WrClk), //input WrClk
		.RdClk(RdClk), //input RdClk
		.WrEn(WrEn), //input WrEn
		.RdEn(RdEn), //input RdEn
		.Q(Q), //output [8:0] Q
		.Empty(Empty), //output Empty
		.Full(Full) //output Full
	);

//--------Copy end-------------------
