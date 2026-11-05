#pragma once
#include <QtGlobal>

// =============================================================================
//  rudp_types.h
//  Константы протокола RUDP. Должны совпадать с rudp_types.sv в RTL.
// =============================================================================
namespace rudp {

// ---- Типы пакетов (первый байт UDP payload) ----
constexpr quint8 TYPE_CMD_ACK  = 0xA1;   // FPGA -> PC, 8 байт
constexpr quint8 TYPE_RESULT   = 0xB1;   // FPGA -> PC, 1029 байт
constexpr quint8 TYPE_RETX     = 0xC1;   // PC  -> FPGA, 32 байта
constexpr quint8 TYPE_COMPLETE = 0xD1;   // PC  -> FPGA, 32 байта

// ---- Размеры пакетов ----
constexpr int CMD_SIZE      = 1024;
constexpr int ACK_SIZE      = 8;         // 5 из них — паддинг eth_vlg
constexpr int RESULT_SIZE   = 1029;      // 5-байтовый заголовок + 1024 payload
constexpr int RESULT_PAYLOAD_SIZE = 1024;

// ---- Минимальный размер коротких управляющих пакетов ----
// eth_vlg на FPGA пропускает только UDP-пакеты длиной >= 32 байт.
// Поэтому RETX и COMPLETE удлинены нулями до 32 байт на проводе.
// Логические поля (type + tid + block) по-прежнему лежат в первых 5 байтах.
constexpr int SHORT_CTRL_SIZE = 32;
constexpr int RETX_SIZE       = SHORT_CTRL_SIZE;
constexpr int COMPLETE_SIZE   = SHORT_CTRL_SIZE;

// ---- Границы полей ----
constexpr int CMD_TID_OFF    = 0;
constexpr int CMD_BLOCKS_OFF = 2;

} // namespace rudp
