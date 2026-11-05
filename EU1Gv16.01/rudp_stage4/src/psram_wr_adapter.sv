// ============================================================
//  psram_wr_adapter.sv
// ============================================================
module psram_wr_adapter (
    input  logic        clk,
    input  logic        rst,
    input  logic        wr_en,
    input  logic [11:0] wr_block,
    input  logic [ 7:0] wr_word_addr,
    input  logic [31:0] wr_data,
    output logic        wr_ready,
    output logic        wr_idle,
    input  logic        flush,

    output logic        psram_cmd,
    output logic        psram_cmd_en,
    output logic [20:0] psram_addr,
    output logic [31:0] psram_wr_data,
    output logic [ 3:0] psram_mask,
    input  logic        psram_cmd_rdy
);
  logic [31:0] word_buf        [0:31];
  logic [ 5:0] buf_cnt;
  logic [ 5:0] send_cnt;
  logic [ 5:0] tcmd_cnt;
  logic        tcmd_wait;
  logic [11:0] current_block;
  logic [ 7:0] first_word_addr;

  typedef enum logic [2:0] {
    IDLE,
    WAIT_RDY,
    SEND_DATA,
    WAIT_TCMD
  } state_t;
  state_t st;

  assign wr_ready = (st == IDLE) && (buf_cnt < 6'd32) && (!tcmd_wait);
  assign wr_idle = (st == IDLE) && (buf_cnt == 0);

  // 12 бит для адреса блока (до 4096)
  assign psram_addr = {current_block[11:0], first_word_addr[7:5], 5'b00000};
  assign psram_mask = 4'b0000;

  always_ff @(posedge clk or posedge rst) begin
    if (rst) begin
      buf_cnt <= '0;
      send_cnt <= '0;
      st <= IDLE;
      tcmd_cnt <= '0;
      tcmd_wait <= 1'b0;
      current_block <= '0;
      psram_cmd <= 1'b0;
      psram_cmd_en <= 1'b0;
      psram_wr_data <= '0;
      first_word_addr <= '0;
    end else begin
      if (wr_en && wr_ready) begin
        word_buf[buf_cnt] <= wr_data;
        if (buf_cnt == 0) first_word_addr <= wr_word_addr;
        buf_cnt <= buf_cnt + 1'b1;
        current_block <= wr_block;
      end

      if (tcmd_wait) begin
        if (tcmd_cnt == 6'd41) begin
          tcmd_wait <= 1'b0;
          tcmd_cnt  <= '0;
        end else begin
          tcmd_cnt <= tcmd_cnt + 1'b1;
        end
      end

      case (st)
        IDLE: begin
          psram_cmd_en <= 1'b0;
          if ((buf_cnt == 6'd32 || (flush && buf_cnt > 0)) && !tcmd_wait) st <= WAIT_RDY;
        end
        WAIT_RDY: begin
          if (psram_cmd_rdy) begin
            psram_cmd <= 1'b1;
            psram_cmd_en <= 1'b1;
            psram_wr_data <= word_buf[0];
            send_cnt <= 6'd1;
            st <= SEND_DATA;
          end
        end
        SEND_DATA: begin
          psram_cmd_en <= 1'b0;
          if (send_cnt == 6'd32) begin
            buf_cnt <= '0;
            tcmd_wait <= 1'b1;
            st <= WAIT_TCMD;
          end else begin
            send_cnt <= send_cnt + 1'b1;
            psram_wr_data <= (send_cnt < buf_cnt) ? word_buf[send_cnt] : 32'h0;
          end
        end
        WAIT_TCMD: begin
          if (!tcmd_wait) st <= IDLE;
        end
        default: st <= IDLE;
      endcase
    end
  end
endmodule
