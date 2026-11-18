// ir_test_top.sv
// One laser tag unit: IR transmit + receive, two interrupt lines to the MCU,
// and an SPI link (the MCU uses it to arm the blaster and read the status).
//
// This board's ID is fixed by MY_ID below. It is the ONLY difference
// between the "Board 1" and "Board 2" folders:
//   Board 1: MY_ID = 4'b1010 (player 1)
//   Board 2: MY_ID = 4'b1100 (player 2)
// Point the IR LED at the TSOP4838 (or bounce it off a wall), press SW0,
// and the board should see its own shot.
//
// Pins (E155 FA23 Development Board v4):
//
//   Signal       FPGA pin  Connects to
//   fire_n       P34       SW0 push button
//   reset_n      P43       SW1 push button
//   test_sw_n    P32       SW6 DIP, 1st switch
//   ir_tx        P13       -> TN2106 gate (IR LED driver)
//   ir_rx_n      P19       <- TSOP4838 OUT
//   hit_int      P27       SW7 switch 4 -> MCU PA6 (also lights D4)
//   fire_int     P18       jumper wire  -> MCU PB0
//   spi_cs_n     P20       SW7 switch 5 <- MCU PA11
//   spi_mosi     P10       SW7 switch 6 <- MCU PB5
//   spi_miso     P12       SW7 switch 7 -> MCU PB4
//   spi_sck      P21       SW7 switch 8 <- MCU PB3 (also lights D5)
//
//   Add a jumper wire from FPGA P18 to MCU PB0 (shot interrupt).
//   SW7 switches 4-8 ON. Switch 3 (PA5 audio) OFF. Switches 1-2 (PA10/PA9,
//   the MCU's USART1) may be ON or OFF: P23/P25 are unused here.
//   SW0, SW1 and SW6 connect to GND, so turn on internal pull-ups for them.
//   (SW6 switch 3, P31, is no longer used: the ID is fixed by MY_ID.)
//
// Behavior:
//   SW0          press to fire (debounced: one shot per press). Ignored while
//                armed = 0 (set over SPI; it is 1 after reset).
//   SW1          hold to reset
//   SW6 (P32)    test mode. OFF = only the OTHER player's shots are hits.
//                ON = every valid packet is a hit, including your own shots
//                (lets one board test hits by shooting its own receiver).
//   D6-D8        not driven (debug LEDs removed)
//   hit_int      1 ms pulse for every hit (rising edge = hit). The MCU plays
//                the hit sound on it. No SPI read is needed.
//   fire_int     high while a packet is being sent (~19 ms). Its rising edge
//                tells the MCU "this unit just fired" (plays the pew).
//
// Status byte the MCU can read over SPI (for later use):
//   bit 7  0      (unused; hits are reported on hit_int)
//   bit 6  0      (unused; shots are reported on fire_int)
//   bit 5  ARMED  current armed setting
//   bit 4  ME_P2  this unit is player 2 (MY_ID == 1100)
//   3:0    ID     shooter ID of the last valid packet (1010 = P1, 1100 = P2)

module ir_test_top (
    input  logic fire_n,       // SW0, active low
    input  logic reset_n,      // SW1, active low
    input  logic test_sw_n,    // SW6 DIP, ON = low = count own shots
    input  logic ir_rx_n,      // TSOP4838 OUT
    output logic ir_tx,        // IR LED driver
    output logic hit_int,      // to MCU PA6: rising edge = hit
    output logic fire_int,     // to MCU PB0: rising edge = shot fired
    input  logic spi_sck,      // from MCU PB3
    input  logic spi_cs_n,     // from MCU PA11
    input  logic spi_mosi,     // from MCU PB5
    output logic spi_miso      // to MCU PB4
);

    // ---------------------------------------------------------------
    // THIS BOARD'S ID. Board 1 = 4'b1010, Board 2 = 4'b1100.
    // ---------------------------------------------------------------
    localparam logic [3:0] MY_ID = 4'b1010;

    localparam logic [3:0] P1_ID = 4'b1010;
    localparam logic [3:0] P2_ID = 4'b1100;   // any MY_ID other than P2_ID acts as P1

    // 24 MHz internal oscillator
    logic clk;

    HSOSC #(.CLKHF_DIV(2'b01))
        hf_osc (.CLKHFPU(1'b1), .CLKHFEN(1'b1), .CLKHF(clk));

    // Synchronize the buttons and switch
    logic fire_meta, fire_sync;
    logic reset_meta, reset_sync;
    logic test_meta, test_sync;

    always_ff @(posedge clk) begin
        test_meta  <= test_sw_n;
        test_sync  <= test_meta;
        fire_meta  <= fire_n;
        fire_sync  <= fire_meta;
        reset_meta <= reset_n;
        reset_sync <= reset_meta;
    end

    logic reset, player_two, test_mode;
    assign reset      = !reset_sync;
    assign player_two = (MY_ID == P2_ID);   // fixed at build time
    assign test_mode  = !test_sync;

    // Debounce SW0. A push button bounces for a few ms when pressed AND when
    // released, which looked like extra presses.
    // The first change is accepted right away (so even a very quick tap
    // fires), then the button is ignored for 20 ms while it bounces. After
    // that, the next change (e.g. the release) is accepted the same way.
    localparam LOCKOUT = 480_000;   // 20 ms at 24 MHz

    logic [18:0] db_count;          // counts down; 0 = ready for a new change
    logic        fire_stable, fire_prev;

    always_ff @(posedge clk) begin
        if (reset) begin
            db_count    <= 0;
            fire_stable <= 1;        // released (active low)
        end else if (db_count != 0) begin
            db_count <= db_count - 1;   // still bouncing: ignore the button
        end else if (fire_sync != fire_stable) begin
            fire_stable <= fire_sync;   // accept the change immediately
            db_count    <= LOCKOUT - 1;
        end
        fire_prev <= fire_stable;
    end

    // SPI link to the MCU (gives us `armed`)
    logic       armed, clear_flags;   // clear_flags unused for now
    logic [7:0] status;

    // Fire only when the button is pressed and the MCU allows it
    logic send;
    assign send = fire_prev && !fire_stable && armed;

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

    // Hit interrupt: a 1 ms pulse for each hit. Packets are at least ~19 ms
    // apart, so every hit gets its own rising edge.
    //   Normal:    only the other player's shots count.
    //   Test mode: every valid packet counts (own shots too).
    localparam HIT_PULSE = 24_000;   // 1 ms at 24 MHz

    logic        hit_event;
    logic [14:0] hit_timer;

    assign hit_event = test_mode  ? (hit_p1 || hit_p2)
                     : player_two ? hit_p1    // we are P2: only P1 shots hit us
                                  : hit_p2;   // we are P1: only P2 shots hit us

    always_ff @(posedge clk) begin
        if (reset)               hit_timer <= 0;
        else if (hit_event)      hit_timer <= HIT_PULSE;
        else if (hit_timer != 0) hit_timer <= hit_timer - 1;
    end

    assign hit_int = (hit_timer != 0);

    assign status = {1'b0, 1'b0, armed, player_two, shooter_id};

    // Shot interrupt: the encoder is busy from the start of the header until
    // the gap after the last bit ends, so one rising edge = one shot.
    assign fire_int = busy;

    mcu_spi spi (
        .clk(clk), .reset(reset),
        .sck(spi_sck), .cs_n(spi_cs_n), .mosi(spi_mosi), .miso(spi_miso),
        .status(status), .clear_flags(clear_flags), .armed(armed)
    );

endmodule
