// ============================================================
//  rudp_rx_frontend.sv
//  Модифицирован для работы с асинхронным FIFO.
//  Читает данные напрямую из FIFO до тех пор, пока не встретит
//  маркер конца пакета (старший бит = 0).
// ============================================================
module rudp_rx_frontend (
    input  logic        clk,
    input  logic        rst,

    // ---- Интерфейс к RX FIFO ----
    input  logic [8:0]  fifo_dout,   // {is_valid, data}
    input  logic        fifo_empty,
    output logic        fifo_rd_en,

    // ---- Разобранный CMD ----
    output logic        cmd_valid,
    output logic [15:0] cmd_tid,
    output logic [12:0] cmd_blocks,

    // ---- Разобранный RETX ----
    output logic        retx_valid,
    output logic [15:0] retx_tid,
    output logic [11:0] retx_block,

    // ---- Разобранный COMPLETE ----
    output logic        complete_valid,
    output logic [15:0] complete_tid
);
    localparam int unsigned CMD_MAX = 1024;
    localparam int unsigned SHORT_CTRL_MIN = 32;

    logic [7:0]  pkt_buf [0:CMD_MAX-1];
    logic [11:0] cnt;
    logic        active;
    
    wire is_valid = !fifo_empty && fifo_dout[8];  // Валидный байт
    wire is_eop   = !fifo_empty && !fifo_dout[8]; // Конец пакета (маркер)

    // Читаем из FIFO всегда, когда там есть данные
    assign fifo_rd_en = !fifo_empty;

    always_ff @(posedge clk or posedge rst) begin
        if (rst) begin
            active         <= 1'b0;
            cnt            <= '0;
            cmd_valid      <= 1'b0;
            retx_valid     <= 1'b0;
            complete_valid <= 1'b0;
            cmd_tid        <= '0;
            cmd_blocks     <= '0;
            retx_tid       <= '0;
            retx_block     <= '0;
            complete_tid   <= '0;
        end
        else begin
            cmd_valid      <= 1'b0;
            retx_valid     <= 1'b0;
            complete_valid <= 1'b0;

            // ---------- Приём байтов ----------
            if (!active) begin
                if (is_valid) begin
                    active     <= 1'b1;
                    cnt        <= 12'd1;
                    pkt_buf[0] <= fifo_dout[7:0];
                end
            end
            else begin
                if (is_valid) begin
                    if (cnt < CMD_MAX)
                        pkt_buf[cnt[$clog2(CMD_MAX)-1:0]] <= fifo_dout[7:0];
                    cnt <= cnt + 1'b1;
                end
                
                // ---------- Конец пакета ----------
                if (is_eop) begin
                    active <= 1'b0;

                    // CMD: ровно 1024 байта
                    if (cnt == 12'd1024) begin
                        cmd_valid  <= 1'b1;
                        cmd_tid    <= {pkt_buf[1], pkt_buf[0]};
                        cmd_blocks <= {pkt_buf[3], pkt_buf[2]};
                    end
                    // Короткие пакеты: минимум 32 байта
                    else if (cnt >= SHORT_CTRL_MIN[$clog2(CMD_MAX):0]) begin
                        if (pkt_buf[0] == 8'hC1) begin
                            retx_valid <= 1'b1;
                            retx_tid   <= {pkt_buf[2], pkt_buf[1]};
                            retx_block <= {pkt_buf[4][3:0], pkt_buf[3]};
                        end
                        else if (pkt_buf[0] == 8'hD1) begin
                            complete_valid <= 1'b1;
                            complete_tid   <= {pkt_buf[2], pkt_buf[1]};
                        end
                    end
                end
            end
        end
    end
endmodule