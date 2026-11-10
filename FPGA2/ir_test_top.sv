// ir_test_top.sv
// Single-FPGA test of ir_encoder + ir_decoder.
// Point the IR LED at the TSOP4838 (or bounce it off a wall), press SW0,
// and the board should see its own shot.
//
// Pins (E155 FA23 Development Board v4 + Breadboard Adapter v4):
//
//   Signal       FPGA pin  On the board                 Breadboard adapter
//   fire_n       P34       SW0 push button              -
//   reset_n      P43       SW1 push button              -
//   player_sw_n  P31       SW6 DIP, 3rd switch          -
//   ir_tx        P20       J6 pin 40                    J3 pin 20  -> TN2106 gate
//   ir_rx_n      P19       J6 pin 35                    J1 pin 3   <- TSOP4838 OUT
//   led_p1       P42       D6 (blue)                    -
//   led_p2       P38       D7 (blue)                    -
//   led_err      P28       D8 (blue)                    -
//
//   SW0, SW1 and SW6 connect to GND, so turn on internal pull-ups for them.
//   P20 is also wired to MCU PA11 through SW7 switch 4. Keep that switch OFF.
//
// Behavior:
//   SW0                  press to fire
//   SW1                  hold to reset
//   SW6 (P31)            OFF = fire as player 1 (1010), ON = player 2 (1100)
//   D6                   lights 0.25 s: received a player 1 shot
//   D7                   lights 0.25 s: received a player 2 shot
//   D8                   lights 0.25 s: received a bad packet

module ir_test_top (
    input  logic fire_n,       // SW0, active low
    input  logic reset_n,      // SW1, active low
    input  logic player_sw_n,  // SW6 DIP, ON = low = player 2
    input  logic ir_rx_n,      // TSOP4838 OUT
    output logic ir_tx,        // IR LED driver
    output logic led_p1,       // D6
    output logic led_p2,       // D7
    output logic led_err       // D8
);

    // 24 MHz internal oscillator
    logic clk;

    HSOSC #(.CLKHF_DIV(2'b01))
        hf_osc (.CLKHFPU(1'b1), .CLKHFEN(1'b1), .CLKHF(clk));

    // Synchronize the buttons and switch
    logic fire_meta, fire_sync, fire_prev;
    logic reset_meta, reset_sync;
    logic sw_meta, sw_sync;

    always_ff @(posedge clk) begin
        fire_meta  <= fire_n;
        fire_sync  <= fire_meta;
        fire_prev  <= fire_sync;
        reset_meta <= reset_n;
        reset_sync <= reset_meta;
        sw_meta    <= player_sw_n;
        sw_sync    <= sw_meta;
    end

    logic reset, send, player_two;
    assign reset      = !reset_sync;
    assign send       = fire_prev && !fire_sync;   // moment the button is pressed
    assign player_two = !sw_sync;

    // Transmitter
    logic busy;

    ir_encoder encoder (
        .clk(clk), .reset(reset), .send(send), .player_two(player_two),
        .ir_tx(ir_tx), .busy(busy)
    );

    // Receiver
    logic hit_p1, hit_p2, rx_error;
    logic [3:0] shooter_id;

    ir_decoder decoder (
        .clk(clk), .reset(reset), .ir_rx_n(ir_rx_n),
        .hit_p1(hit_p1), .hit_p2(hit_p2),
        .shooter_id(shooter_id), .rx_error(rx_error)
    );

    // Keep each LED on for 0.25 s (6,000,000 cycles) so you can see it
    logic [22:0] p1_timer, p2_timer, err_timer;

    always_ff @(posedge clk) begin
        if (reset) begin
            p1_timer  <= 0;
            p2_timer  <= 0;
            err_timer <= 0;
        end else begin
            if (hit_p1)             p1_timer <= 6_000_000;
            else if (p1_timer != 0) p1_timer <= p1_timer - 1;

            if (hit_p2)             p2_timer <= 6_000_000;
            else if (p2_timer != 0) p2_timer <= p2_timer - 1;

            if (rx_error)            err_timer <= 6_000_000;
            else if (err_timer != 0) err_timer <= err_timer - 1;
        end
    end

    assign led_p1  = (p1_timer  != 0);
    assign led_p2  = (p2_timer  != 0);
    assign led_err = (err_timer != 0);

endmodule
