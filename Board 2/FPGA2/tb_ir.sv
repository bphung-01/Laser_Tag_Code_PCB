// tb_ir.sv
// Connects ir_encoder to ir_decoder through a rough TSOP4838 model and
// checks that player 1 and player 2 shots are decoded correctly.

`timescale 1ns/1ps

module tb_ir;

    logic clk = 0;
    always #20.833 clk = ~clk;   // 24 MHz

    logic reset = 1;
    logic send = 0;
    logic player_two = 0;
    logic ir_tx, busy;

    ir_encoder enc (.clk(clk), .reset(reset), .send(send),
                    .player_two(player_two), .ir_tx(ir_tx), .busy(busy));

    // TSOP model: output goes low while carrier is present, and stays low
    // 80 us after the carrier stops (the real part stretches bursts too).
    realtime last_carrier = -1s;
    always @(posedge ir_tx) last_carrier = $realtime;

    logic ir_rx_n;
    always @(posedge clk) ir_rx_n <= !(($realtime - last_carrier) < 80us);

    logic hit_p1, hit_p2, rx_error;
    logic [3:0] shooter_id;

    ir_decoder dec (.clk(clk), .reset(reset), .ir_rx_n(ir_rx_n),
                    .hit_p1(hit_p1), .hit_p2(hit_p2),
                    .shooter_id(shooter_id), .rx_error(rx_error));

    int p1_hits = 0, p2_hits = 0, errors = 0;
    always @(posedge clk) begin
        if (hit_p1)   p1_hits++;
        if (hit_p2)   p2_hits++;
        if (rx_error) errors++;
    end

    task fire(input logic p2);
        @(posedge clk) begin player_two <= p2; send <= 1; end
        @(posedge clk) send <= 0;
        wait (busy);
        wait (!busy);
    endtask

    initial begin
        repeat (10) @(posedge clk);
        reset = 0;

        fire(0);
        $display("Player 1 fired: p1_hits=%0d p2_hits=%0d id=%b", p1_hits, p2_hits, shooter_id);
        fire(1);
        $display("Player 2 fired: p1_hits=%0d p2_hits=%0d id=%b", p1_hits, p2_hits, shooter_id);
        fire(0);
        $display("Player 1 fired: p1_hits=%0d p2_hits=%0d id=%b", p1_hits, p2_hits, shooter_id);

        if (p1_hits == 2 && p2_hits == 1 && errors == 0) $display("PASS");
        else $display("FAIL (errors=%0d)", errors);
        $finish;
    end

endmodule
