FPGA Ethernet Stack for GOWIN — with RUDP

English | Русский

A SystemVerilog-based Ethernet and network stack for GOWIN FPGAs, extended with RUDP (Reliable UDP) functionality.

The project is intended for FPGA-based network devices that require direct Ethernet connectivity and reliable transfer of application data over UDP.

    Development status: Hardware testing has been performed on GOWIN GW2AR-18. Core functionality and multiple RUDP scenarios have been verified in simulation and on real hardware. Further integration and long-duration testing are ongoing.

Features

    FPGA platform: GOWIN, including the GW2AR-18 family.

    HDL: SystemVerilog.

    Ethernet: RGMII interface with support for 100 Mbps and 1 Gbps operation, subject to the PHY, clocking, timing constraints, and board configuration.

    Network protocols:

        Ethernet MAC

        ARP — Address Resolution Protocol

        IPv4 — Internet Protocol version 4

        ICMP — Internet Control Message Protocol

        DHCP — Dynamic Host Configuration Protocol

        DNS — Domain Name System

        UDP — User Datagram Protocol

        RUDP — Reliable User Datagram Protocol

    Verification: QuestaSim simulation and hardware tests on GOWIN GW2AR-18, including RUDP boundary, recovery, control-packet, and stress tests.

    Hardware integration: Ethernet PHY and FPGA MAC interface.

Protocol support and test coverage may vary between modules. The project is still being debugged and should not yet be considered a production-ready networking stack.
Network stack overview

The main data path can be represented as:
text

                 Ethernet PHY
                      |
                     RGMII
                      |
                  Ethernet MAC
                      |
                 IPv4 / ARP
                      |
                +-----+-----+
                |           |
               ICMP       UDP
                            |
                           RUDP
                            |
                    Application logic

ARP supports local network address resolution. DHCP provides network configuration, DNS provides name resolution, and ICMP supports network diagnostics such as ping. IPv4 carries network packets, while UDP provides datagram transport for applications.

The MAC and RGMII interface connect the FPGA logic to a compatible external Ethernet PHY.
RUDP — Reliable UDP

RUDP is a lightweight application-level protocol implemented on top of UDP. It adds transaction identification, acknowledgements, block numbering, retransmission requests, and completion handling.

Unlike TCP, this implementation is designed around application-defined commands and fixed-size result blocks.
Current protocol design

    Command (CMD): 1024 bytes.

    Transaction ID (TID): 16-bit identifier.

    Command acknowledgement (CMD_ACK): confirms acceptance of a valid command.

    Result data (RESULT): 1024-byte payload per block.

    Retransmission request (RETX): requests a particular result block again.

    Completion (COMPLETE): informs the FPGA that the host has completed receiving the results.

    Block count: 1 to 4096 result blocks per transaction.

    Result storage: designed for up to 4096 × 1024 bytes of result data.

    Transaction handling: one active command at a time.

    Timeout and recovery: timeout handling, retransmission, and recovery from reset are implemented and tested in simulation and on hardware.

The current implementation uses a custom packet format and is not intended to be wire-compatible with unrelated RUDP implementations.
RUDP packet formats

The protocol defines five message types. All multi-byte fields are transmitted in little-endian order unless otherwise noted.
CMD — Command (PC → FPGA)

Total size: 1024 bytes
Byte offset	Field	Size (bytes)	Description
0	TID_LO	1	Transaction ID, low byte
1	TID_HI	1	Transaction ID, high byte
2	N_LO	1	Number of result blocks, low byte
3	N_HI[4:0]	1	Number of result blocks, high bits (only lower 5 bits used)
4–1023	Reserved / padding	1020	Reserved for future use

TID — transaction identifier; N — number of result blocks. The command has a fixed length of 1024 bytes.
CMD_ACK — Command acknowledgement (FPGA → PC)

Total size: 4 bytes
Byte offset	Field	Size (bytes)	Description
0	TID_LO	1	Transaction ID, low byte
1	TID_HI	1	Transaction ID, high byte
2	Status	1	0 = command accepted; other values reserved
3	Reserved	1	Reserved for future use
RESULT — Result block (FPGA → PC)

Total size: 1028 bytes (4-byte header + 1024-byte payload)
Byte offset	Field	Size (bytes)	Description
0	TID_LO	1	Transaction ID, low byte
1	TID_HI	1	Transaction ID, high byte
2	BLOCK_LO	1	Result block number, low byte
3	BLOCK_HI[4:0]	1	Result block number, high bits (only lower 5 bits used)
4–1027	Data	1024	Result block payload
RETX — Retransmission request (PC → FPGA)

Total size: 4 bytes
Byte offset	Field	Size (bytes)	Description
0	TID_LO	1	Transaction ID, low byte
1	TID_HI	1	Transaction ID, high byte
2	BLOCK_LO	1	Requested result block number, low byte
3	BLOCK_HI[4:0]	1	Requested result block number, high bits (only lower 5 bits used)
COMPLETE — Completion notification (PC → FPGA)

Total size: 4 bytes
Byte offset	Field	Size (bytes)	Description
0	TID_LO	1	Transaction ID, low byte
1	TID_HI	1	Transaction ID, high byte
2	Reserved	1	Reserved for future use
3	Reserved	1	Reserved for future use
RUDP verification status

The following simulation stages have passed:
Test stage	Verified functionality
3D	Timeout handling and recovery
3E-1	Block-count boundaries, including 1 and 4096 blocks
3E-2	Reset recovery during execution, transmission, and completion wait
3E-3	Duplicate commands, invalid control packets, TID validation, and retransmissions
3F	100 sequential transactions with retransmission and malformed-control scenarios
3G	Larger result transfers and retransmission stress, including 256-block transactions

The regression tests reported zero scoreboard errors. Initial hardware tests on GOWIN GW2AR-18 confirmed basic RUDP transaction flow, including command acceptance, result transfer, and retransmission under controlled conditions. These results validate the tested scenarios; they do not replace comprehensive packet-loss, timing, throughput, and long-duration tests on physical hardware.
Project structure

The project is organized as follows. The exact directory layout may evolve during development.
text

.
├── src/                 # Synthesizable RTL
│   ├── Ethernet MAC and RGMII interface
│   ├── Network protocol logic
│   └── RUDP implementation
├── ver/                 # Verification-related files
├── tb/                  # Simulation testbenches
├── hw/                  # FPGA and board-specific examples
├── hdl_generics/        # Generic HDL components
└── README.md

Important RUDP RTL modules include:
Module	Purpose
rudp_types.sv	Shared protocol definitions and state types
rudp_rx_parser.sv	Incoming RUDP packet parsing
cmd_fsm.sv	Command processing state machine
exec_engine.sv	Application command execution interface
result_ram.sv	Result block storage
rudp_tx.sv	Outgoing RUDP packet generation
rudp_core.sv	Main RUDP control and transaction state machine

Module names and file locations may change as the project develops.
Development and simulation

The current development environment includes:

    GOWIN FPGA tools (Gowin IDE).

    GOWIN GW2AR-18 target hardware.

    QuestaSim-64 2021.1.

    SystemVerilog RTL and dedicated simulation testbenches.

The RUDP testbenches cover command acceptance, result generation, retransmission, malformed control packets, timeout behavior, and reset recovery.

Refer to the project scripts and testbench files for the exact compilation and simulation commands. Hardware configuration, pin assignments, PLL settings, and timing constraints depend on the target board and PHY.
Current limitations

    The project is under active development and debugging.

    Hardware behavior has been partially validated but requires further testing under real packet loss, varying timing, and long-duration load.

    Ethernet speed depends on the PHY, board design, clock configuration, and timing closure.

    RUDP is a custom protocol and currently supports one active command transaction at a time.

    Full protocol compliance, interoperability, throughput limits, and long-duration reliability have not yet been comprehensively established.

Roadmap

    Continue hardware debugging on GOWIN FPGA boards.

    Validate RUDP command and result transfer over a physical Ethernet connection under various conditions.

    Test retransmission and recovery under real packet loss.

    Measure sustained throughput and behavior under long-duration load.

    Improve documentation, integration examples, and reproducibility of tests.

Disclaimer

This is an experimental FPGA networking project. Features, interfaces, and implementation details may change. Use in production systems requires additional validation.
Русский
Ethernet-стек для ПЛИС GOWIN с поддержкой RUDP

Проект сетевого стека на SystemVerilog для ПЛИС GOWIN, расширенный поддержкой RUDP (Reliable UDP).

Проект предназначен для FPGA-устройств, которым требуется прямое подключение к Ethernet и надёжная передача прикладных данных поверх UDP.

    Статус разработки: проведено тестирование на реальном оборудовании GOWIN GW2AR-18. Основные функции и несколько сценариев RUDP проверены в симуляции и на железе. Интеграция и длительные испытания продолжаются.

Возможности

    Платформа: ПЛИС GOWIN, в том числе семейство GW2AR-18.

    Язык описания аппаратуры: SystemVerilog.

    Ethernet: интерфейс RGMII с поддержкой режимов 100 Мбит/с и 1 Гбит/с при соответствующей конфигурации PHY, тактирования и временных ограничений.

    Реализованные сетевые протоколы:

        Ethernet MAC

        ARP — разрешение сетевых адресов

        IPv4 — сетевой протокол версии 4

        ICMP — служебные сообщения и диагностика сети

        DHCP — автоматическая настройка сетевых параметров

        DNS — разрешение доменных имён

        UDP — передача датаграмм

        RUDP — надёжная передача данных поверх UDP

    Верификация: симуляция QuestaSim и тесты на оборудовании GOWIN GW2AR-18, включая проверку границ, повторных передач, тайм-аутов и восстановления после сброса.

    Аппаратная интеграция: интерфейс MAC/RGMII для подключения совместимого Ethernet PHY.

Покрытие тестами и степень готовности могут различаться для отдельных модулей. Проект продолжает отлаживаться и пока не позиционируется как готовый промышленный сетевой стек.
Структура сетевого стека

Основной путь передачи данных:
text

                 Ethernet PHY
                      |
                     RGMII
                      |
                  Ethernet MAC
                      |
                 IPv4 / ARP
                      |
                +-----+-----+
                |           |
               ICMP       UDP
                            |
                           RUDP
                            |
                    Прикладная логика

ARP обеспечивает разрешение MAC-адресов в локальной сети. DHCP используется для настройки сетевых параметров, DNS — для разрешения доменных имён, ICMP — для диагностики сети, включая ping. IPv4 обеспечивает сетевую доставку пакетов, а UDP используется для передачи датаграмм.

MAC и интерфейс RGMII обеспечивают связь логики ПЛИС с совместимым внешним Ethernet PHY.
RUDP — надёжная передача данных поверх UDP

RUDP — собственный прикладной протокол поверх UDP. Он добавляет идентификатор транзакции, подтверждение команды, нумерацию блоков, запрос повторной передачи и сообщение о завершении приёма.

В отличие от TCP, текущая реализация ориентирована на обмен прикладными командами и блоками данных фиксированного размера.
Текущие параметры протокола

    Команда (CMD): 1024 байта.

    Идентификатор транзакции (TID): 16 бит.

    Подтверждение команды (CMD_ACK): подтверждает принятие корректной команды.

    Результат (RESULT): 1024 байта полезных данных на блок.

    Повторная передача (RETX): запрос повторной отправки конкретного блока.

    Завершение (COMPLETE): сообщает ПЛИС, что хост закончил приём результатов.

    Количество блоков: от 1 до 4096 на одну транзакцию.

    Хранение результатов: предусмотрено до 4096 × 1024 байт.

    Обработка команд: одновременно выполняется одна активная транзакция.

    Тайм-ауты и восстановление: реализованы и проверены в симуляции и на железе.

Используется собственный формат пакетов. Совместимость с другими реализациями RUDP не предполагается.
Форматы RUDP-пакетов

Протокол определяет пять типов сообщений. Все многобайтовые поля передаются в порядке little-endian, если не указано иное.
CMD — команда (ПК → ПЛИС)

Общий размер: 1024 байта
Смещение (байт)	Поле	Размер (байт)	Назначение
0	TID_LO	1	Идентификатор транзакции, младший байт
1	TID_HI	1	Идентификатор транзакции, старший байт
2	N_LO	1	Количество блоков результата, младший байт
3	N_HI[4:0]	1	Количество блоков результата, старшие биты (используются только младшие 5 бит)
4–1023	Reserved / padding	1020	Зарезервировано

TID — идентификатор транзакции; N — количество блоков результата. Команда имеет фиксированную длину 1024 байта.
CMD_ACK — подтверждение команды (ПЛИС → ПК)

Общий размер: 4 байта
Смещение (байт)	Поле	Размер (байт)	Назначение
0	TID_LO	1	Идентификатор транзакции, младший байт
1	TID_HI	1	Идентификатор транзакции, старший байт
2	Status	1	0 = команда принята; остальные значения зарезервированы
3	Reserved	1	Зарезервировано
RESULT — блок результата (ПЛИС → ПК)

Общий размер: 1028 байт (4 байта заголовка + 1024 байта данных)
Смещение (байт)	Поле	Размер (байт)	Назначение
0	TID_LO	1	Идентификатор транзакции, младший байт
1	TID_HI	1	Идентификатор транзакции, старший байт
2	BLOCK_LO	1	Номер блока результата, младший байт
3	BLOCK_HI[4:0]	1	Номер блока результата, старшие биты (используются только младшие 5 бит)
4–1027	Data	1024	Полезные данные блока результата
RETX — запрос повторной передачи (ПК → ПЛИС)

Общий размер: 4 байта
Смещение (байт)	Поле	Размер (байт)	Назначение
0	TID_LO	1	Идентификатор транзакции, младший байт
1	TID_HI	1	Идентификатор транзакции, старший байт
2	BLOCK_LO	1	Запрашиваемый номер блока, младший байт
3	BLOCK_HI[4:0]	1	Запрашиваемый номер блока, старшие биты (используются только младшие 5 бит)
COMPLETE — завершение приёма (ПК → ПЛИС)

Общий размер: 4 байта
Смещение (байт)	Поле	Размер (байт)	Назначение
0	TID_LO	1	Идентификатор транзакции, младший байт
1	TID_HI	1	Идентификатор транзакции, старший байт
2	Reserved	1	Зарезервировано
3	Reserved	1	Зарезервировано
Результаты симуляции и тестирования RUDP

Успешно пройдены следующие этапы:
Этап	Что проверено
3D	Тайм-ауты и восстановление
3E-1	Границы количества блоков, включая 1 и 4096
3E-2	Восстановление после сброса во время выполнения, передачи и ожидания завершения
3E-3	Дублирующиеся команды, некорректные управляющие пакеты, проверка TID и повторные передачи
3F	100 последовательных транзакций с повторными передачами и некорректными управляющими пакетами
3G	Передача больших наборов результатов и нагрузочные тесты RETX, включая транзакции по 256 блоков

Во всех указанных тестах ошибок scoreboard не обнаружено. Первичные испытания на GOWIN GW2AR-18 подтвердили базовый сценарий RUDP: приём команды, передачу результатов и повторные передачи в контролируемых условиях. Эти результаты подтверждают корректность проверенных сценариев, но не заменяют всесторонние испытания на реальном оборудовании с потерями пакетов, измерением пропускной способности и длительной работой.
Структура проекта

Проект организован следующим образом. Структура каталогов может меняться по мере разработки.
text

.
├── src/                 # Синтезируемый RTL-код
│   ├── Ethernet MAC и RGMII
│   ├── Сетевые протоколы
│   └── Реализация RUDP
├── ver/                 # Материалы для верификации
├── tb/                  # Тестовые стенды
├── hw/                  # Примеры для ПЛИС и плат
├── hdl_generics/        # Универсальные HDL-компоненты
└── README.md

Основные модули RUDP:
Модуль	Назначение
rudp_types.sv	Общие определения протокола и типов состояний
rudp_rx_parser.sv	Разбор входящих пакетов RUDP
cmd_fsm.sv	Автомат обработки команд
exec_engine.sv	Интерфейс выполнения прикладной команды
result_ram.sv	Хранение блоков результатов
rudp_tx.sv	Формирование исходящих пакетов RUDP
rudp_core.sv	Основной автомат управления транзакциями RUDP

Имена модулей и расположение файлов могут изменяться по мере развития проекта.
Разработка и симуляция

Текущая среда разработки:

    Gowin IDE.

    Целевая ПЛИС GOWIN GW2AR-18.

    QuestaSim-64 2021.1.

    RTL на SystemVerilog и специализированные тестовые стенды.

Тесты RUDP охватывают принятие команд, формирование результатов, повторные передачи, некорректные управляющие пакеты, тайм-ауты и восстановление после сброса.

Точные команды компиляции и запуска симуляции следует смотреть в скриптах проекта и тестовых стендах. Настройки выводов, PLL и временных ограничений зависят от конкретной платы и Ethernet PHY.
Текущие ограничения

    Проект активно разрабатывается и отлаживается.

    Поведение на реальном оборудовании частично проверено, но требует дальнейших испытаний при потерях пакетов, изменении таймингов и длительной нагрузке.

    Доступная скорость Ethernet зависит от PHY, схемотехники платы, тактирования и выполнения временных ограничений.

    RUDP является собственным протоколом; текущая реализация поддерживает одну активную транзакцию за раз.

    Полное соответствие сетевым стандартам, совместимость с другими реализациями, пределы производительности и долговременная надёжность ещё не проверены исчерпывающим образом.

Планы развития

    Продолжить отладку на ПЛИС GOWIN.

    Проверить передачу команд и результатов RUDP через физическое Ethernet-соединение в различных условиях.

    Испытать повторные передачи и восстановление при реальных потерях пакетов.

    Измерить устойчивую пропускную способность и поведение при длительной нагрузке.

    Улучшить документацию, примеры интеграции и воспроизводимость тестов.

Отказ от гарантий

Это экспериментальный сетевой проект для ПЛИС. Функции, интерфейсы и детали реализации могут изменяться. 
Для применения в промышленной системе необходима дополнительная проверка.