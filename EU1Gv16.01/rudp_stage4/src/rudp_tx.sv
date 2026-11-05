// ============================================================
//  rudp_tx.sv
//  Добавлен вход tx_fifo_full: останавливает выдачу в DATA и старт в IDLE,
//  если TX FIFO заполнена. Это устраняет потери при передаче 4096 блоков,
//  когда rudp_tx (50 МГц) генерирует быстрее, чем MAC (25 МГц) читает.
// ============================================================
module rudp_tx #(
    parameter int PAUSE_LEN1G   = 64,
    parameter int PAUSE_LEN100M = 64 * 18
) (
    input  logic        clk,
    input  logic        rst,
    input  logic        speedis1000_i,
    input  logic        start_ack,
    input  logic        start_result,
    input  logic        start_retx,
    input  logic [15:0] transaction_id,
    input  logic [11:0] block_index,
    input  logic [ 7:0] ram_data,
    output logic [11:0] ram_block,
    output logic [ 9:0] ram_addr,
    output logic [15:0] udp_len,
    output logic [ 7:0] udp_din,
    output logic        udp_vin,
    input  logic        udp_cts,
    input  logic        tx_fifo_full,    // <-- backpressure из TX FIFO
    output logic        busy,
    output logic        done
);
  typedef enum logic [2:0] {
    IDLE,
    HDR,
    DATA,
    WAIT_CTS,
    PAUSE
  } st_t;
  st_t         st;
  logic [15:0] pos;
  logic [11:0] blk;
  logic [15:0] pause_cnt;
  logic        udp_cts_d;

  wire         udp_cts_pos = udp_cts && !udp_cts_d;
  wire  [15:0] pause_len = speedis1000_i ? PAUSE_LEN1G[15:0] : PAUSE_LEN100M[15:0];

  always_ff @(posedge clk or posedge rst)
    if (rst) udp_cts_d <= 1'b0;
    else udp_cts_d <= udp_cts;

  always_ff @(posedge clk or posedge rst) begin
    if (rst) begin
      st <= IDLE;
      pos <= '0;
      blk <= '0;
      pause_cnt <= '0;
      udp_len <= '0;
      udp_din <= '0;
      udp_vin <= 1'b0;
      busy <= 1'b0;
      done <= 1'b0;
      ram_block <= '0;
      ram_addr <= '0;
    end else begin
      udp_vin <= 1'b0;
      done    <= 1'b0;

      case (st)
        IDLE: begin
          busy <= 1'b0;
          // Ждём старта и свободного места в FIFO
          if ((start_ack || start_result || start_retx) && !tx_fifo_full) begin
            busy <= 1'b1;
            udp_len <= start_ack ? 16'd8 : 16'd1029;
            pos <= '0;
            blk <= block_index;
            st <= HDR;
          end
        end

        HDR: begin
          // Заголовок всегда отправляем — он короткий (5 или 8 байт),
          // FIFO заведомо успеет их принять.
          udp_vin <= 1'b1;
          if (udp_len == 16'd8) begin
            // ---- CMD_ACK ----
            case (pos)
              16'd0:   udp_din <= rudp_types::TYPE_CMD_ACK;
              16'd1:   udp_din <= transaction_id[7:0];
              16'd2:   udp_din <= transaction_id[15:8];
              default: udp_din <= 8'h00;
            endcase
            if (pos == 16'd7) st <= WAIT_CTS;
            else pos <= pos + 16'd1;
          end else begin
            // ---- RESULT ----
            case (pos)
              16'd0:   udp_din <= rudp_types::TYPE_RESULT;
              16'd1:   udp_din <= transaction_id[7:0];
              16'd2:   udp_din <= transaction_id[15:8];
              16'd3:   udp_din <= blk[7:0];
              default: udp_din <= {4'b0, blk[11:8]};
            endcase

            if (pos == 16'd3) begin
              ram_block <= blk;
              ram_addr  <= 10'd0;
              pos       <= pos + 16'd1;
            end else if (pos == 16'd4) begin
              ram_addr <= 10'd0;
              pos      <= 16'd0;
              st       <= DATA;
            end else begin
              pos <= pos + 16'd1;
            end
          end
        end

        DATA: begin
          // ФИКС: пауза выдачи при заполненной FIFO
          if (!tx_fifo_full) begin
            udp_vin <= 1'b1;
            udp_din <= ram_data;

            if (pos == 16'd1023) begin
              st <= WAIT_CTS;
            end else begin
              pos      <= pos + 16'd1;
              ram_addr <= pos[9:0] + 10'd1;
            end
          end
        end

        WAIT_CTS: begin
          busy <= 1'b1;
          if (udp_cts_pos) begin
            pause_cnt <= pause_len;
            st        <= PAUSE;
          end
        end

        PAUSE: begin
          busy <= 1'b1;
          if (pause_cnt == 0) begin
            done <= 1'b1;
            st   <= IDLE;
          end else begin
            pause_cnt <= pause_cnt - 1'b1;
          end
        end

        default: st <= IDLE;
      endcase
    end
  end
endmodule
