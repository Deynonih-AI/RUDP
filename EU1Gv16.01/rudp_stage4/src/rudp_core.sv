// ============================================================
//  rudp_core.sv
//  TX FIFO углублена до 4096 байт (ADDR_WIDTH=12).
//  В rudp_tx подаётся tx_fifo_full для backpressure.
//
//  FIX 2026-10-28:
//   1) в ST_WAIT_COMPLETE при обработке RETX теперь обновляется
//      tx_block (без этого заголовок RESULT-ответа на RETX содержал
//      устаревший block_index);
//   2) RETX-запросы буферизуются в кольцевую FIFO глубины 16
//      (раньше был одиночный регистр — при пачке RETX от PC
//      обрабатывался только последний).
// ============================================================
module rudp_core #(
    parameter integer TIMEOUT_CYCLES = 100000000,
    parameter int     PAUSE_LEN1G    = 64,
    parameter int     PAUSE_LEN100M  = 64 * 18
) (
    input  logic                           clk,
    input  logic                           clk_psram,
    input  logic                           rst,
    input  logic                           soft_rst,
    input  logic                           speedis1000_i,
    input  logic                    [ 7:0] udp_dout,
    input  logic                           udp_vout,
    output logic                    [15:0] udp_len,
    output logic                    [ 7:0] udp_din,
    output logic                           udp_vin,
    input  logic                           udp_cts,
    output logic                           psram_cmd,
    output logic                           psram_cmd_en,
    output logic                    [20:0] psram_addr,
    output logic                    [31:0] psram_wr_data,
    output logic                    [ 3:0] psram_mask,
    input  logic                           psram_cmd_rdy,
    input  logic                    [31:0] psram_rd_data,
    input  logic                           psram_rd_valid,
    input  logic                           init_calib0,
    output logic                           transaction_busy,
    output rudp_types::core_state_t        state_dbg,
    output logic                    [31:0] timeout_counter_dbg,
    output logic                           timeout_event
);
  import rudp_types::*;
  core_state_t st;

  // ---------------- CDC синхронизаторы ----------------
  logic init_calib_psram, udp_cts_psram;
  cdc_sync u_sync_init (
      .clk_dst(clk_psram),
      .rst_dst(rst),
      .sig_src(init_calib0),
      .sig_dst(init_calib_psram)
  );
  cdc_sync u_sync_cts (
      .clk_dst(clk_psram),
      .rst_dst(rst),
      .sig_src(udp_cts),
      .sig_dst(udp_cts_psram)
  );

  // ---------------- RX FIFO (MAC -> PSRAM) ----------------
  logic rx_fifo_empty, rx_fifo_rd_en, udp_vout_d, udp_vout_fall, rx_fifo_wr_en;
  logic [8:0] rx_fifo_dout, rx_fifo_din;

  always_ff @(posedge clk or posedge rst)
    if (rst) udp_vout_d <= 1'b0;
    else udp_vout_d <= udp_vout;

  assign udp_vout_fall = udp_vout_d & ~udp_vout;
  assign rx_fifo_wr_en = udp_vout | udp_vout_fall;
  assign rx_fifo_din   = udp_vout ? {1'b1, udp_dout} : {1'b0, 8'h00};

  // async_fifo #(
  //     .DATA_WIDTH(9),
  //     .ADDR_WIDTH(10)
  // ) u_rx_fifo (
  //     .wr_clk(clk),
  //     .wr_rst(rst),
  //     .wr_en(rx_fifo_wr_en),
  //     .din(rx_fifo_din),
  //     .full(),
  //     .rd_clk(clk_psram),
  //     .rd_rst(rst),
  //     .rd_en(rx_fifo_rd_en),
  //     .dout(rx_fifo_dout),
  //     .empty(rx_fifo_empty)
  // );

  logic       rx_fifo_full;    // для будущей диагностики (не используется в логике)

  fifo_rx_hs u_rx_fifo (
      .Data (rx_fifo_din),    // [8:0] input: {is_valid, data}
      .WrClk(clk),
      .WrEn (rx_fifo_wr_en),
      .Full (rx_fifo_full),

      .Q    (rx_fifo_dout),   // [8:0] output
      .RdClk(clk_psram),
      .RdEn (rx_fifo_rd_en),
      .Empty(rx_fifo_empty),

      .Reset(rst)
  );

  // ---------------- TX FIFO (PSRAM -> MAC) ----------------
  logic tx_fifo_full, tx_fifo_wr_en, tx_fifo_empty, tx_fifo_rd_en;
  logic [7:0] tx_fifo_din, tx_fifo_dout;

  // async_fifo #(
  //     .DATA_WIDTH(8),
  //     .ADDR_WIDTH(11)  // 4096 байт
  // ) u_tx_fifo (
  //     .wr_clk(clk_psram),
  //     .wr_rst(rst),
  //     .wr_en(tx_fifo_wr_en),
  //     .din(tx_fifo_din),
  //     .full(tx_fifo_full),
  //     .rd_clk(clk),
  //     .rd_rst(rst),
  //     .rd_en(tx_fifo_rd_en),
  //     .dout(tx_fifo_dout),
  //     .empty(tx_fifo_empty)
  // );

  fifo_tx_hs u_tx_fifo (
      .Data (tx_fifo_din),    // [7:0] input
      .WrClk(clk_psram),
      .WrEn (tx_fifo_wr_en),
      .Full (tx_fifo_full),

      .Q    (tx_fifo_dout),   // [7:0] output
      .RdClk(clk),
      .RdEn (tx_fifo_rd_en),
      .Empty(tx_fifo_empty),

      .Reset(rst)  // один общий сброс
  );

  // ---------------- RX Frontend ----------------
  logic cmd_valid, retx_valid, complete_valid;
  logic [15:0] cmd_tid_rx, retx_tid, complete_tid;
  logic [12:0] cmd_blocks_rx;
  logic [11:0] retx_block;
  logic [15:0] tid;
  logic [12:0] nblocks;

  rudp_rx_frontend u_front (
      .clk(clk_psram),
      .rst(rst | soft_rst),
      .fifo_dout(rx_fifo_dout),
      .fifo_empty(rx_fifo_empty),
      .fifo_rd_en(rx_fifo_rd_en),
      .cmd_valid(cmd_valid),
      .cmd_tid(cmd_tid_rx),
      .cmd_blocks(cmd_blocks_rx),
      .retx_valid(retx_valid),
      .retx_tid(retx_tid),
      .retx_block(retx_block),
      .complete_valid(complete_valid),
      .complete_tid(complete_tid)
  );

  // ---------------- Exec Engine ----------------
  logic exec_start, exec_busy, exec_done, exec_wr_en, exec_wr_ready;
  logic [11:0] exec_wr_block;
  logic [ 7:0] exec_wr_word_addr;
  logic [31:0] exec_wr_data;

  exec_engine u_exec (
      .clk(clk_psram),
      .rst(rst | soft_rst),
      .start(exec_start),
      .block_count(nblocks),
      .wr_ready(exec_wr_ready),
      .busy(exec_busy),
      .done(exec_done),
      .wr_en(exec_wr_en),
      .wr_block(exec_wr_block),
      .wr_word_addr(exec_wr_word_addr),
      .wr_data(exec_wr_data)
  );

  // ---------------- PSRAM WR Adapter ----------------
  logic wr_psram_cmd, wr_psram_cmd_en, wr_idle;
  logic [20:0] wr_psram_addr;
  logic [31:0] wr_psram_data;
  logic [ 3:0] wr_psram_mask;

  psram_wr_adapter u_wr (
      .clk(clk_psram),
      .rst(rst | soft_rst),
      .wr_en(exec_wr_en),
      .wr_block(exec_wr_block),
      .wr_word_addr(exec_wr_word_addr),
      .wr_data(exec_wr_data),
      .wr_ready(exec_wr_ready),
      .wr_idle(wr_idle),
      .flush(exec_done),
      .psram_cmd(wr_psram_cmd),
      .psram_cmd_en(wr_psram_cmd_en),
      .psram_addr(wr_psram_addr),
      .psram_wr_data(wr_psram_data),
      .psram_mask(wr_psram_mask),
      .psram_cmd_rdy(psram_cmd_rdy)
  );

  logic wr_active;
  always_ff @(posedge clk_psram or posedge rst) begin
    if (rst) wr_active <= 1'b0;
    else if (exec_start) wr_active <= 1'b1;
    else if (wr_idle && !exec_busy) wr_active <= 1'b0;
  end

  // ---------------- PSRAM RD Prefetch ----------------
  logic start_prefetch, prefetch_busy, prefetch_done, prefetch_done_d;
  logic [11:0] prefetch_block;
  logic rd_psram_cmd, rd_psram_cmd_en;
  logic [20:0] rd_psram_addr;
  logic [11:0] tx_block;
  logic [ 9:0] ram_addr;
  logic [ 7:0] ram_data;

  psram_rd_prefetch u_rd (
      .clk(clk_psram),
      .rst(rst | soft_rst),
      .start_prefetch(start_prefetch),
      .prefetch_block(prefetch_block),
      .prefetch_busy(prefetch_busy),
      .prefetch_done(prefetch_done),
      .ram_block(tx_block),
      .ram_addr(ram_addr),
      .ram_data(ram_data),
      .psram_cmd(rd_psram_cmd),
      .psram_cmd_en(rd_psram_cmd_en),
      .psram_addr(rd_psram_addr),
      .psram_cmd_rdy(psram_cmd_rdy),
      .psram_rd_data(psram_rd_data),
      .psram_rd_valid(psram_rd_valid)
  );

  always_ff @(posedge clk_psram or posedge rst) begin
    if (rst) prefetch_done_d <= 1'b0;
    else prefetch_done_d <= prefetch_done;
  end

  // ---------------- PSRAM mux ----------------
  assign psram_cmd    = wr_active ? wr_psram_cmd : rd_psram_cmd;
  assign psram_cmd_en = wr_active ? wr_psram_cmd_en : rd_psram_cmd_en;
  assign psram_addr   = wr_active ? wr_psram_addr : rd_psram_addr;
  assign psram_wr_data= wr_psram_data;
  assign psram_mask   = wr_psram_mask;

  // ---------------- TX Engine ----------------
  logic tx_start_ack, tx_start_result, tx_start_retx, tx_busy, tx_done, tx_vin;
  logic [15:0] tx_len;
  logic [ 7:0] tx_din;

  rudp_tx #(
      .PAUSE_LEN1G  (PAUSE_LEN1G),
      .PAUSE_LEN100M(PAUSE_LEN100M)
  ) u_tx (
      .clk(clk_psram),
      .rst(rst | soft_rst),
      .speedis1000_i(speedis1000_i),
      .start_ack(tx_start_ack),
      .start_result(tx_start_result),
      .start_retx(tx_start_retx),
      .transaction_id(tid),
      .block_index(tx_block),
      .ram_data(ram_data),
      .ram_block(),
      .ram_addr(ram_addr),
      .udp_len(tx_len),
      .udp_din(tx_din),
      .udp_vin(tx_vin),
      .udp_cts(udp_cts_psram),
      .tx_fifo_full(tx_fifo_full),
      .busy(tx_busy),
      .done(tx_done)
  );

  assign tx_fifo_wr_en = tx_vin;
  assign tx_fifo_din   = tx_din;
  assign tx_fifo_rd_en = !tx_fifo_empty;
  assign udp_din       = tx_fifo_dout;
  assign udp_vin       = !tx_fifo_empty;
  assign udp_len       = tx_len;

  // ============================================================
  //  RETX FIFO (кольцевая, 16 записей)
  //  Буферизует запросы RETX от PC. Раньше был одиночный регистр —
  //  при пачке RETX обрабатывался только последний, остальные PC
  //  повторял через RETX-интервал, что замедляло восстановление.
  // ============================================================
  localparam int RETX_FIFO_LOG2 = 4;
  localparam int RETX_FIFO_DEPTH = 1 << RETX_FIFO_LOG2;  // 16

  logic [11:0] retx_fifo_mem[0:RETX_FIFO_DEPTH-1];
  logic [RETX_FIFO_LOG2:0] retx_fifo_wr_ptr;  // 5 бит: 4 индекса + 1 доп.
  logic [RETX_FIFO_LOG2:0] retx_fifo_rd_ptr;

  // Стандартная ring-проверка: пусто если указатели равны (с доп. битом).
  // Полно если старшие биты разные, а младшие равны.
  wire retx_fifo_empty = (retx_fifo_wr_ptr == retx_fifo_rd_ptr);
  wire retx_fifo_full  = (retx_fifo_wr_ptr[RETX_FIFO_LOG2]
                          != retx_fifo_rd_ptr[RETX_FIFO_LOG2])
                         && (retx_fifo_wr_ptr[RETX_FIFO_LOG2-1:0]
                             == retx_fifo_rd_ptr[RETX_FIFO_LOG2-1:0]);

  // Push: валидный RETX с нашим TID и валидным block, FIFO не полна.
  wire retx_push_now = transaction_busy
                       && retx_valid
                       && (retx_tid == tid)
                       && (retx_block < nblocks)
                       && !retx_fifo_full;

  // Pop: FSM в ST_WAIT_COMPLETE, tx закончил, нет COMPLETE, FIFO не пуста.
  wire retx_pop_now = (st == ST_WAIT_COMPLETE) && !tx_busy && !complete_pending && !retx_fifo_empty;

  // Отдельный always_ff для указателей FIFO — независим от основного FSM.
  // Push и pop могут произойти в одном такте: указатели инкрементируются
  // независимо, данные не теряются.
  always_ff @(posedge clk_psram or posedge rst) begin
    if (rst || soft_rst) begin
      retx_fifo_wr_ptr <= '0;
      retx_fifo_rd_ptr <= '0;
    end else begin
      // Сброс FIFO при старте новой транзакции
      if (st == ST_IDLE && init_calib_psram && cmd_valid
          && (cmd_blocks_rx >= 13'd1) && (cmd_blocks_rx <= 13'd4096)) begin
        retx_fifo_wr_ptr <= '0;
        retx_fifo_rd_ptr <= '0;
      end else begin
        if (retx_push_now) begin
          retx_fifo_mem[retx_fifo_wr_ptr[RETX_FIFO_LOG2-1:0]] <= retx_block;
          retx_fifo_wr_ptr <= retx_fifo_wr_ptr + 1'b1;
        end
        if (retx_pop_now) begin
          retx_fifo_rd_ptr <= retx_fifo_rd_ptr + 1'b1;
        end
      end
    end
  end

  // ---------------- Main FSM ----------------
  logic   [12:0] next_block;
  integer        timeout_cnt;
  logic complete_pending, ack_sent;
  logic exec_done_seen;

  always_ff @(posedge clk_psram or posedge rst) begin
    if (rst || soft_rst) exec_done_seen <= 1'b0;
    else if (exec_start) exec_done_seen <= 1'b0;
    else if (exec_done) exec_done_seen <= 1'b1;
  end

  assign transaction_busy    = (st != ST_IDLE);
  assign state_dbg           = st;
  assign timeout_counter_dbg = timeout_cnt;

  always_ff @(posedge clk_psram or posedge rst or posedge soft_rst) begin
    if (rst || soft_rst) begin
      st <= ST_IDLE;
      tid <= '0;
      nblocks <= '0;
      exec_start <= 1'b0;
      start_prefetch <= 1'b0;
      tx_start_ack <= 1'b0;
      tx_start_result <= 1'b0;
      tx_start_retx <= 1'b0;
      next_block <= '0;
      tx_block <= '0;
      prefetch_block <= '0;
      timeout_cnt <= 0;
      timeout_event <= 1'b0;
      complete_pending <= 1'b0;
      ack_sent <= 1'b0;
    end else begin
      exec_start      <= 1'b0;
      start_prefetch  <= 1'b0;
      tx_start_ack    <= 1'b0;
      tx_start_result <= 1'b0;
      tx_start_retx   <= 1'b0;
      timeout_event   <= 1'b0;

      // Запрос на COMPLETE от PC — фиксируем флаг
      if (transaction_busy) begin
        if (complete_valid && complete_tid == tid) begin
          complete_pending <= 1'b1;
        end
      end

      case (st)
        ST_IDLE: begin
          timeout_cnt <= 0;
          if (init_calib_psram && cmd_valid &&
              (cmd_blocks_rx >= 13'd1) && (cmd_blocks_rx <= 13'd4096)) begin
            tid <= cmd_tid_rx;
            nblocks <= cmd_blocks_rx;
            tx_start_ack <= 1'b1;
            complete_pending <= 1'b0;
            ack_sent <= 1'b0;
            st <= ST_SEND_ACK;
          end
        end

        ST_SEND_ACK: begin
          if (tx_done) ack_sent <= 1'b1;
          if (ack_sent) begin
            exec_start <= 1'b1;
            st         <= ST_EXEC;
          end
        end

        ST_EXEC: begin
          if (exec_done_seen) begin
            if (wr_idle) begin
              if (timeout_cnt >= 64) begin
                next_block  <= '0;
                st          <= ST_PREFETCH_RES;
                timeout_cnt <= 0;
              end else begin
                timeout_cnt <= timeout_cnt + 1;
              end
            end else begin
              timeout_cnt <= 0;
            end
          end
        end

        ST_PREFETCH_RES: begin
          if (!prefetch_busy) begin
            prefetch_block <= next_block[11:0];
            start_prefetch <= 1'b1;
            st <= ST_WAIT_PREFETCH_RES;
          end
        end

        ST_WAIT_PREFETCH_RES: begin
          if (prefetch_done && !prefetch_done_d) begin
            tx_block        <= next_block[11:0];
            tx_start_result <= 1'b1;
            st              <= ST_TX_RES;
          end
        end

        ST_TX_RES: begin
          if (tx_done) begin
            if (next_block == nblocks - 1'b1) begin
              timeout_cnt <= 0;
              st <= ST_WAIT_COMPLETE;
            end else begin
              next_block <= next_block + 1'b1;
              st <= ST_PREFETCH_RES;
            end
          end
        end

        ST_WAIT_COMPLETE: begin
          if (tx_busy) begin
            // ждём завершения отправки
          end else if (complete_pending) begin
            complete_pending <= 1'b0;
            timeout_cnt <= 0;
            st <= ST_IDLE;
          end else if (retx_pop_now) begin
            // FIX: берём следующий RETX из FIFO
            prefetch_block <= retx_fifo_mem[retx_fifo_rd_ptr[RETX_FIFO_LOG2-1:0]];
            tx_block       <= retx_fifo_mem[retx_fifo_rd_ptr[RETX_FIFO_LOG2-1:0]];
            start_prefetch <= 1'b1;
            st             <= ST_WAIT_PREFETCH_RETX;
          end else if (timeout_cnt >= TIMEOUT_CYCLES - 1) begin
            timeout_cnt <= 0;
            timeout_event <= 1'b1;
            st <= ST_IDLE;
          end else begin
            timeout_cnt <= timeout_cnt + 1;
          end
        end

        ST_WAIT_PREFETCH_RETX: begin
          if (prefetch_done && !prefetch_done_d) begin
            tx_start_retx <= 1'b1;
            st <= ST_SEND_RETX;
          end
        end

        ST_SEND_RETX: begin
          if (tx_done) begin
            timeout_cnt <= 0;
            st <= ST_WAIT_COMPLETE;
          end
        end

        default: st <= ST_IDLE;
      endcase
    end
  end
endmodule
