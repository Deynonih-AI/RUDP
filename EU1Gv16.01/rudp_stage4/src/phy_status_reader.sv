module phy_status_reader #(
    parameter PHY_ADDR = 5'd0,//5'd1,
    parameter CLK_FREQ_HZ = 25_000_000,
    parameter POLL_MS = 1000//200
)(
    input  logic        clk,
    input  logic        rst,

    // MIIM interface MAC
    output logic        miim_read,
    output logic        miim_write,
    output logic [4:0]  miim_phy_addr,
    output logic [4:0]  miim_reg_addr,
    output logic [15:0] miim_wr_data,
    input  logic [15:0] miim_rd_data,
    input  logic        miim_busy,
    input  logic        miim_done,

    // outputs to MAC
    output logic        speedis1000,
    output logic        speedis10,
    output logic        duplex,
    output logic        link_up
);

    localparam POLL_CYCLES = (CLK_FREQ_HZ / 1000) * POLL_MS;

    typedef enum logic [3:0] {
        RESET_S,
        POLL_WAIT,
        READ_BMSR_1,
        WAIT_BMSR_1,
        READ_BMSR_2,
        WAIT_BMSR_2,
        CHECK_LINK,
        READ_PSSR,
        WAIT_PSSR,
        UPDATE
    } state_t;

    state_t state;

    logic [31:0] poll_cnt;
    logic [15:0] bmsr;
    logic [15:0] pssr;

    // регистры состояния (чтобы видеть изменение)
    logic link_up_r;
    logic speed1000_r;
    logic speed10_r;
    logic duplex_r;

    //--------------------------------------------------------
    // FSM
    //--------------------------------------------------------

    always_ff @(posedge clk or posedge rst) begin
        if (rst) begin
            state <= RESET_S;
            poll_cnt <= 0;
            miim_read <= 0;
            miim_write <= 0;
            miim_phy_addr <= PHY_ADDR;

            link_up_r <= 0;
            speed1000_r <= 0;
            speed10_r <= 0;
            duplex_r <= 0;

        end else begin

            miim_read  <= 0;
            miim_write <= 0;

            case (state)

            //--------------------------------------------------
            RESET_S:
                state <= POLL_WAIT;

            //--------------------------------------------------
            POLL_WAIT: begin
                if (poll_cnt >= POLL_CYCLES) begin
                    poll_cnt <= 0;
                    state <= READ_BMSR_1;
                end else
                    poll_cnt <= poll_cnt + 1;
            end

            //--------------------------------------------------
            // 1-е чтение BMSR (discard)
            READ_BMSR_1: begin
                if (!miim_busy) begin
                    miim_phy_addr <= PHY_ADDR;
                    miim_reg_addr <= 5'h01;
                    miim_read <= 1;
                    state <= WAIT_BMSR_1;
                end
            end

            WAIT_BMSR_1:
                if (miim_done)
                    state <= READ_BMSR_2;

            //--------------------------------------------------
            // 2-е чтение BMSR (валидное)
            READ_BMSR_2: begin
                if (!miim_busy) begin
                    miim_reg_addr <= 5'h01;
                    miim_read <= 1;
                    state <= WAIT_BMSR_2;
                end
            end

            WAIT_BMSR_2:
                if (miim_done) begin
                    bmsr <= miim_rd_data;
                    state <= CHECK_LINK;
                end

            //--------------------------------------------------
            CHECK_LINK:
                if (bmsr[2])   //(miim_rd_data[2])   // Link Status bit
                    state <= READ_PSSR;
                else begin
                    link_up_r   <= 0;
                    speed1000_r <= 0;
                    speed10_r   <= 0;
                    duplex_r    <= 0;
                    state <= POLL_WAIT;
                end

            //--------------------------------------------------
            READ_PSSR: begin
                miim_phy_addr <= PHY_ADDR;
                miim_reg_addr <= 5'h11;
                miim_read <= 1;
                state <= WAIT_PSSR;
            end

            WAIT_PSSR:
                if (miim_done) begin
                    pssr <= miim_rd_data;
                    state <= UPDATE;
                end

            //--------------------------------------------------
            UPDATE: begin
                link_up_r <= 1;
                duplex_r  <= pssr[13

]; //pssr[11];

                case (pssr[15:14]) //(pssr[13:12])
                    2'b10: begin
                        speed1000_r <= 1;
                        speed10_r   <= 0;
                    end
                    2'b01: begin
                        speed1000_r <= 0;
                        speed10_r   <= 0; // 100M
                    end
                    2'b00: begin
                        speed1000_r <= 0;
                        speed10_r   <= 1;
                    end
                    default: begin
                        speed1000_r <= 0;
                        speed10_r   <= 0;
                    end
                endcase

                state <= POLL_WAIT;
            end

            endcase
        end
    end

    //--------------------------------------------------------
    // Выходы
    //--------------------------------------------------------

    assign link_up     = link_up_r;
    assign duplex      = duplex_r;
    assign speedis1000 = speed1000_r;
    assign speedis10   = speed10_r;

endmodule