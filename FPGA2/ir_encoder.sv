// ir_encoder.sv
// Sends one IR packet each time `send` goes high. Runs on a 24 MHz clock.
//
// Player 1 ID = 1010, Player 2 ID = 1100
//
// Packet: header, then 8 bits = {ID, ~ID}, MSB first.
//   header = 1.8 ms of carrier
//   bit 1  = 1.2 ms of carrier
//   bit 0  = 0.6 ms of carrier
//   after every burst: 0.6 ms off
//   after the last bit: 4.8 ms off before another shot is allowed
//
// Timing at 24 MHz: 0.6 ms = 14,400 clock cycles.

module ir_encoder (
    input  logic clk,          // 24 MHz
    input  logic reset,        // active high
    input  logic send,         // trigger pulse
    input  logic player_two,   // 0 = player 1, 1 = player 2
    output logic ir_tx,        // to IR LED driver
    output logic busy
);

    // Burst and space lengths in clock cycles
    localparam HEADER = 43_200;    // 1.8 ms
    localparam ONE    = 28_800;    // 1.2 ms
    localparam ZERO   = 14_400;    // 0.6 ms
    localparam SPACE  = 14_400;    // 0.6 ms
    localparam GAP    = 115_200;   // 4.8 ms

    localparam P1_ID = 4'b1010;
    localparam P2_ID = 4'b1100;

    // States
    localparam IDLE = 2'd0;
    localparam ON   = 2'd1;    // carrier on
    localparam OFF  = 2'd2;    // carrier off

    logic [1:0]  state;
    logic [16:0] timer;      // counts down to 0
    logic [7:0]  data;       // bits still to send, MSB goes next
    logic [3:0]  bits_left;  // data bits not yet sent

    assign busy = (state != IDLE);

    always_ff @(posedge clk) begin
        if (reset) begin
            state     <= IDLE;
            timer     <= 0;
            data      <= 0;
            bits_left <= 0;
        end else begin
            case (state)
                IDLE: begin
                    if (send) begin
                        if (player_two) data <= {P2_ID, ~P2_ID};
                        else            data <= {P1_ID, ~P1_ID};
                        bits_left <= 8;
                        timer     <= HEADER - 1;
                        state     <= ON;
                    end
                end

                ON: begin
                    if (timer == 0) begin
                        if (bits_left == 0) timer <= GAP - 1;   // last burst done
                        else                timer <= SPACE - 1;
                        state <= OFF;
                    end else begin
                        timer <= timer - 1;
                    end
                end

                OFF: begin
                    if (timer == 0) begin
                        if (bits_left == 0) begin
                            state <= IDLE;
                        end else begin
                            if (data[7]) timer <= ONE - 1;
                            else         timer <= ZERO - 1;
                            data      <= data << 1;
                            bits_left <= bits_left - 1;
                            state     <= ON;
                        end
                    end else begin
                        timer <= timer - 1;
                    end
                end

                default: state <= IDLE;
            endcase
        end
    end

    // 38 kHz carrier: toggle every 316 cycles (24 MHz / 632 = 37.97 kHz).
    // It restarts at the beginning of each burst.
    logic [8:0] carrier_count;
    logic       carrier;

    always_ff @(posedge clk) begin
        if (reset || state != ON) begin
            carrier_count <= 0;
            carrier       <= 1;
        end else if (carrier_count == 315) begin
            carrier_count <= 0;
            carrier       <= ~carrier;
        end else begin
            carrier_count <= carrier_count + 1;
        end
    end

    always_ff @(posedge clk) begin
        if (reset) ir_tx <= 0;
        else       ir_tx <= (state == ON) && carrier;
    end

endmodule
