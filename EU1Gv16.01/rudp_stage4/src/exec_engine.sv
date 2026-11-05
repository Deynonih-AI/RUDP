// ============================================================
//  exec_engine.sv
// ============================================================
module exec_engine (
    input  logic        clk,
    input  logic        rst,
    input  logic        start,
    input  logic [12:0] block_count,
    input  logic        wr_ready,
    output logic        busy,
    output logic        done,

    output logic        wr_en,
    output logic [11:0] wr_block,
    output logic [ 7:0] wr_word_addr,
    output logic [31:0] wr_data
);
  logic [12:0] b;
  logic [ 9:0] a;

  assign wr_en = busy && wr_ready;
  assign wr_block = b[11:0];
  assign wr_word_addr = a[9:2];
  assign wr_data = {
    8'(b[7:0] + a[7:0] + 8'd3),
    8'(b[7:0] + a[7:0] + 8'd2),
    8'(b[7:0] + a[7:0] + 8'd1),
    8'(b[7:0] + a[7:0])
  };

  always_ff @(posedge clk or posedge rst) begin
    if (rst) begin
      busy <= 1'b0;
      done <= 1'b0;
      b    <= '0;
      a    <= '0;
    end else begin
      done <= 1'b0;
      if (start && !busy) begin
        busy <= 1'b1;
        b    <= '0;
        a    <= '0;
      end else if (busy) begin
        if (wr_ready) begin
          if (a == 10'd1020) begin
            a <= '0;
            if (b == block_count - 1'b1) begin
              busy <= 1'b0;
              done <= 1'b1;
            end else begin
              b <= b + 1'b1;
            end
          end else begin
            a <= a + 4;
          end
        end
      end
    end
  end
endmodule
