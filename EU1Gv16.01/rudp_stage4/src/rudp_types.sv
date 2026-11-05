// ============================================================
//  rudp_types.sv
// ============================================================
package rudp_types;

  typedef logic [15:0] transaction_id_t;
  typedef logic [11:0] block_index_t;
  typedef logic [12:0] block_count_t;

  // ---- Состояния главного автомата rudp_core ----
  typedef enum logic [3:0] {
    ST_IDLE,
    ST_EXEC,
    ST_SEND_ACK,
    ST_PREFETCH_RES,        // Новое: запуск префетча
    ST_WAIT_PREFETCH_RES,   // Новое: ожидание префетча
    ST_TX_RES,              // Новое: отправка результата
    ST_WAIT_COMPLETE,
    ST_WAIT_PREFETCH_RETX,  // Новое: префетч для RETX
    ST_SEND_RETX
  } core_state_t;

  localparam logic [7:0] TYPE_CMD_ACK = 8'hA1;
  localparam logic [7:0] TYPE_RESULT = 8'hB1;
  localparam logic [7:0] TYPE_RETX = 8'hC1;
  localparam logic [7:0] TYPE_COMPLETE = 8'hD1;

endpackage
