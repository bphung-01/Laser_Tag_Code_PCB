// ir_decoder.sv
// Decodes packets from ir_encoder.sv using a TSOP4838. Runs on a 24 MHz clock.
//
// The TSOP4838 output is LOW while it sees 38 kHz carrier and HIGH otherwise.
// We time how long each low pulse lasts and decide what it was:
//
//   shorter than 0.3 ms  -> noise
//   0.3 to 0.9 ms        -> bit 0     (sent as 0.6 ms)
//   0.9 to 1.5 ms        -> bit 1     (sent as 1.2 ms)
//   1.5 to 2.1 ms        -> header    (sent as 1.8 ms)
//   longer than 2.1 ms   -> noise
//
// After a header, 8 bits are collected: {ID, ~ID}. The packet is good only
// if the second half is the complement of the first and the ID belongs to a
// player. Then hit_p1 or hit_p2 pulses high for one clock.

module ir_decoder (
    input  logic       clk,        // 24 MHz
    input  logic       reset,      // active high
    input  logic       ir_rx_n,    // TSOP4838 output
    output logic       hit_p1,     // 1-clock pulse: shot came from player 1
    output logic       hit_p2,     // 1-clock pulse: shot came from player 2
    output logic [3:0] shooter_id, // ID from the last good packet
    output logic       rx_error    // 1-clock pulse: bad packet (for debugging)
);

    // Thresholds in clock cycles (24 cycles = 1 us)
    localparam MIN_BIT   = 7_200;    // 0.3 ms
    localparam ZERO_ONE  = 21_600;   // 0.9 ms
    localparam ONE_HDR   = 36_000;   // 1.5 ms
    localparam MAX_HDR   = 50_400;   // 2.1 ms
    localparam TIMEOUT   = 28_800;   // 1.2 ms of no carrier mid-packet

    localparam P1_ID = 4'b1010;
    localparam P2_ID = 4'b1100;

    // Synchronizer, plus the previous value for edge detection
    logic rx_meta, rx_sync, rx_prev;

    always_ff @(posedge clk) begin
        rx_meta <= ir_rx_n;
        rx_sync <= rx_meta;
        rx_prev <= rx_sync;
    end

    logic burst_start, burst_end;
    assign burst_start = rx_prev  && !rx_sync;   // high -> low
    assign burst_end   = !rx_prev && rx_sync;    // low -> high

    // Counts clock cycles since the last edge. Stops at its maximum value.
    logic [16:0] count;

    always_ff @(posedge clk) begin
        if (reset || burst_start || burst_end)
            count <= 0;
        else if (count != 17'h1FFFF)
            count <= count + 1;
    end

    // What kind of burst just ended? (count still holds its length)
    logic is_zero, is_one, is_header;
    assign is_zero   = (count >= MIN_BIT)  && (count < ZERO_ONE);
    assign is_one    = (count >= ZERO_ONE) && (count < ONE_HDR);
    assign is_header = (count >= ONE_HDR)  && (count < MAX_HDR);

    logic       receiving;
    logic [7:0] data;
    logic [3:0] bit_count;

    // The full 8 bits, including the one that just arrived
    logic [7:0] packet;
    assign packet = {data[6:0], is_one};

    logic [3:0] id, id_inv;
    assign id     = packet[7:4];
    assign id_inv = packet[3:0];

    always_ff @(posedge clk) begin
        if (reset) begin
            receiving  <= 0;
            data       <= 0;
            bit_count  <= 0;
            hit_p1     <= 0;
            hit_p2     <= 0;
            shooter_id <= 0;
            rx_error   <= 0;
        end else begin
            hit_p1   <= 0;
            hit_p2   <= 0;
            rx_error <= 0;

            // No carrier for too long in the middle of a packet: give up.
            if (receiving && rx_sync && rx_prev && count >= TIMEOUT) begin
                receiving <= 0;
                rx_error  <= 1;
            end

            else if (burst_end) begin
                if (is_header) begin
                    // A header always starts a new packet.
                    receiving <= 1;
                    data      <= 0;
                    bit_count <= 0;
                end

                else if (receiving) begin
                    if (is_zero || is_one) begin
                        data      <= packet;
                        bit_count <= bit_count + 1;

                        if (bit_count == 7) begin
                            // That was the last bit. Check the packet.
                            receiving <= 0;
                            if (id == ~id_inv && id == P1_ID) begin
                                hit_p1     <= 1;
                                shooter_id <= id;
                            end else if (id == ~id_inv && id == P2_ID) begin
                                hit_p2     <= 1;
                                shooter_id <= id;
                            end else begin
                                rx_error <= 1;
                            end
                        end
                    end else begin
                        // Burst was too short or too long.
                        receiving <= 0;
                        rx_error  <= 1;
                    end
                end
                // Bursts before a header are ignored.
            end
        end
    end

endmodule
