module cdc_sync #(
    parameter int STAGES = 2
) (
    input  logic clk_dst,
    input  logic rst_dst,
    input  logic sig_src,
    output logic sig_dst
);
  (* async_reg = "true" *) logic [STAGES-1:0] sync_reg;
  always_ff @(posedge clk_dst or posedge rst_dst) begin
    if (rst_dst) sync_reg <= '0;
    else sync_reg <= {sync_reg[STAGES-2:0], sig_src};
  end
  assign sig_dst = sync_reg[STAGES-1];
endmodule
