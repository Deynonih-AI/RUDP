// ============================================================
//  psram_rd_prefetch.sv
// ============================================================
module psram_rd_prefetch (
    input  logic        clk,
    input  logic        rst,
    input  logic        start_prefetch,
    input  logic [11:0] prefetch_block,
    output logic        prefetch_busy,
    output logic        prefetch_done,
    input  logic [11:0] ram_block,
    input  logic [ 9:0] ram_addr,
    output logic [ 7:0] ram_data,
    output logic        psram_cmd,
    output logic        psram_cmd_en,
    output logic [20:0] psram_addr,
    input  logic        psram_cmd_rdy,
    input  logic [31:0] psram_rd_data,
    input  logic        psram_rd_valid
);
  logic [7:0] byte_buf[0:1023];
  typedef enum logic [2:0] {
    IDLE,
    WAIT_RDY,
    WAIT_DATA,
    WAIT_TCMD
  } state_t;
  state_t st;
  logic [2:0] burst_idx;
  logic [5:0] word_cnt;
  logic [5:0] tcmd_cnt;

  assign prefetch_busy = (st != IDLE);
  assign prefetch_done = (st == WAIT_DATA && burst_idx == 3'd7 && word_cnt == 6'd31 && psram_rd_valid);

  // 12 бит для адреса блока
  assign psram_addr = {prefetch_block[11:0], burst_idx, 5'b00000};

  // Комбинационное чтение из памяти
  assign ram_data = byte_buf[ram_addr];

  always_ff @(posedge clk or posedge rst) begin
    if (rst) begin
      st <= IDLE;
      burst_idx <= '0;
      word_cnt <= '0;
      psram_cmd <= 1'b0;
      psram_cmd_en <= 1'b0;
      tcmd_cnt <= '0;
    end else begin
      case (st)
        IDLE: begin
          psram_cmd_en <= 1'b0;
          if (start_prefetch) begin
            burst_idx <= '0;
            word_cnt <= '0;
            tcmd_cnt <= '0;
            st <= WAIT_RDY;
          end
        end
        WAIT_RDY: begin
          if (psram_cmd_rdy) begin
            psram_cmd <= 1'b0;
            psram_cmd_en <= 1'b1;
            word_cnt <= '0;
            st <= WAIT_DATA;
          end
        end
        WAIT_DATA: begin
          psram_cmd_en <= 1'b0;
          if (psram_rd_valid) begin
            byte_buf[{burst_idx, word_cnt[4:0], 2'b00}] <= psram_rd_data[7:0];
            byte_buf[{burst_idx, word_cnt[4:0], 2'b01}] <= psram_rd_data[15:8];
            byte_buf[{burst_idx, word_cnt[4:0], 2'b10}] <= psram_rd_data[23:16];
            byte_buf[{burst_idx, word_cnt[4:0], 2'b11}] <= psram_rd_data[31:24];

            if (word_cnt == 6'd31) begin
              word_cnt <= '0;
              if (burst_idx == 3'd7) st <= IDLE;
              else begin
                burst_idx <= burst_idx + 1'b1;
                tcmd_cnt <= '0;
                st <= WAIT_TCMD;
              end
            end else word_cnt <= word_cnt + 1'b1;
          end
        end
        WAIT_TCMD: begin
          if (tcmd_cnt == 6'd41) st <= WAIT_RDY;
          else tcmd_cnt <= tcmd_cnt + 1'b1;
        end
      endcase
    end
  end
endmodule
