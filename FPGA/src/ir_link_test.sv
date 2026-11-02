module ir_link_test (
    output logic ir_tx,       // P20: to 100-ohm resistor and TN2106 gate
    input  logic ir_rx_n,     // P19: TSOP4838 OUT, active low
    output logic detected_n   // P42: onboard D6 LED, active low
);

    logic clk;

    // E155 UPduino internal oscillator: 24 MHz with this divider.
    HSOSC #(.CLKHF_DIV(2'b01)) oscillator (
        .CLKHFPU(1'b1),
        .CLKHFEN(1'b1),
        .CLKHF(clk)
    );

    // 24 MHz / (2 * 316) = approximately 37.97 kHz.
    logic [8:0] carrier_count = 0;
    logic carrier = 0;

    always_ff @(posedge clk) begin
        if (carrier_count == 315) begin
            carrier_count <= 0;
            carrier <= ~carrier;
        end else begin
            carrier_count <= carrier_count + 1'b1;
        end
    end

    // Repeat every 100 ms. For the first 24 ms, send
    // twenty 600-us bursts separated by 600-us gaps.
    logic [21:0] frame_count = 0;
    logic [14:0] burst_count = 0;

    always_ff @(posedge clk) begin
        if (frame_count == 2_399_999)
            frame_count <= 0;
        else
            frame_count <= frame_count + 1'b1;

        if (burst_count == 28_799)
            burst_count <= 0;
        else
            burst_count <= burst_count + 1'b1;
    end

    assign ir_tx = (frame_count < 576_000) &&
                   (burst_count < 14_400) &&
                   carrier;

    // Synchronize the receiver output before using it.
    logic rx_meta = 1;
    logic rx_sync = 1;

    always_ff @(posedge clk) begin
        rx_meta <= ir_rx_n;
        rx_sync <= rx_meta;
    end

    // Keep D6 lit for about 0.25 s after a detected burst.
    logic [22:0] light_timer = 0;

    always_ff @(posedge clk) begin
        if (!rx_sync)
            light_timer <= 6_000_000;
        else if (light_timer != 0)
            light_timer <= light_timer - 1'b1;
    end

    assign detected_n = (light_timer != 0);

endmodule