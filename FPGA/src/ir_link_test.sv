module ir_link_test (
    input  logic trigger_n,   // P23: onboard SW1, active low
    input  logic ir_rx_n,     // P19: TSOP4838 OUT, active low
    output logic ir_tx,       // P20: TN2106 gate through 100 ohms
    output logic detected_n   // P42: onboard D6
);

    logic clk;

    // 24 MHz internal FPGA clock.
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

    // Synchronize the pushbutton and detect each new press.
    logic trigger_meta = 1;
    logic trigger_sync = 1;
    logic trigger_prev = 1;

    always_ff @(posedge clk) begin
        trigger_meta <= trigger_n;
        trigger_sync <= trigger_meta;
        trigger_prev <= trigger_sync;
    end

    logic new_press;
    assign new_press = trigger_prev && !trigger_sync;

    // A press sends 20 bursts. Each burst is 600 us on,
    // followed by 600 us off: 24 ms total.
    logic [19:0] tx_remaining = 0;
    logic [14:0] burst_count = 0;

    always_ff @(posedge clk) begin
        if (tx_remaining == 0) begin
            burst_count <= 0;

            if (new_press)
                tx_remaining <= 576_000;
        end else begin
            tx_remaining <= tx_remaining - 1'b1;

            if (burst_count == 28_799)
                burst_count <= 0;
            else
                burst_count <= burst_count + 1'b1;
        end
    end

    // Send the carrier only during each burst.
    assign ir_tx = (tx_remaining != 0) &&
                   (burst_count < 14_400) &&
                   carrier;

    // Synchronize the IR receiver output.
    logic rx_meta = 1;
    logic rx_sync = 1;

    always_ff @(posedge clk) begin
        rx_meta <= ir_rx_n;
        rx_sync <= rx_meta;
    end

    // Hold the detection indicator for 0.25 seconds.
    logic [22:0] light_timer = 0;

    always_ff @(posedge clk) begin
        if (!rx_sync)
            light_timer <= 6_000_000;
        else if (light_timer != 0)
            light_timer <= light_timer - 1'b1;
    end
	
    // D6 appeared to light when its FPGA output was high
    // in your first test, so drive it high on detection.
    assign detected_n = (light_timer != 0);

endmodule