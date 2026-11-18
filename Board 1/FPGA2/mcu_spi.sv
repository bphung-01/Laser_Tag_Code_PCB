// mcu_spi.sv
// SPI slave so the MCU can read events from the FPGA and send it commands.
// Runs on the 24 MHz clock and oversamples the SPI pins (SCK up to ~2 MHz;
// the MCU uses 1.25 MHz).
//
// SPI mode 0: MOSI/MISO change on SCK falling edges, are sampled on rising
// edges. MSB first. CS is active low.
//
// Transaction:
//   CS low
//   byte 0   MCU -> FPGA: command          FPGA -> MCU: status byte
//   byte 1   MCU -> FPGA: argument (opt.)  FPGA -> MCU: 0
//   CS high
//
// Commands:
//   0x00         read status (1 byte is enough)
//   0x01 <ctrl>  write control register; ctrl[0] = armed
//
// The status byte is captured when CS goes low. `clear_flags` pulses at the
// same moment, so the event flags the MCU just read are cleared.

module mcu_spi (
    input  logic       clk,          // 24 MHz
    input  logic       reset,        // active high
    input  logic       sck,
    input  logic       cs_n,
    input  logic       mosi,
    output logic       miso,
    input  logic [7:0] status,       // sampled when CS goes low
    output logic       clear_flags,  // 1-clock pulse when status is sampled
    output logic       armed         // control register bit 0
);

    localparam CMD_WRITE_CTRL = 8'h01;

    // Synchronizers, plus previous values for edge detection
    logic [2:0] sck_s, cs_s;
    logic [1:0] mosi_s;

    always_ff @(posedge clk) begin
        sck_s  <= {sck_s[1:0],  sck};
        cs_s   <= {cs_s[1:0],   cs_n};
        mosi_s <= {mosi_s[0],   mosi};
    end

    logic sck_rise, sck_fall, cs_fall, selected, mosi_bit;
    assign sck_rise = !sck_s[2] &&  sck_s[1];
    assign sck_fall =  sck_s[2] && !sck_s[1];
    assign cs_fall  =  cs_s[2]  && !cs_s[1];
    assign selected = !cs_s[1];
    assign mosi_bit =  mosi_s[1];

    logic [7:0] tx_shift, rx_shift, command;
    logic [2:0] bit_count;
    logic       first_byte;

    assign clear_flags = cs_fall;
    assign miso        = tx_shift[7];

    always_ff @(posedge clk) begin
        if (reset) begin
            tx_shift   <= 0;
            rx_shift   <= 0;
            command    <= 0;
            bit_count  <= 0;
            first_byte <= 1;
            armed      <= 1;          // fire normally until the MCU says otherwise
        end else if (cs_fall) begin
            tx_shift   <= status;     // MSB is on MISO before the first SCK edge
            bit_count  <= 0;
            first_byte <= 1;
        end else if (selected) begin
            if (sck_rise) begin
                rx_shift  <= {rx_shift[6:0], mosi_bit};
                bit_count <= bit_count + 1;

                if (bit_count == 7) begin
                    // {rx_shift[6:0], mosi_bit} is the complete byte
                    if (first_byte) begin
                        command    <= {rx_shift[6:0], mosi_bit};
                        first_byte <= 0;
                    end else if (command == CMD_WRITE_CTRL) begin
                        armed <= mosi_bit;   // ctrl[0] is the last bit in
                    end
                end
            end

            if (sck_fall) begin
                tx_shift <= {tx_shift[6:0], 1'b0};   // next bit out (0s after status)
            end
        end
    end

endmodule
