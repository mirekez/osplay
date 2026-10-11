module OSplayNetBridge (
  input clock, reset,
  input net_tx_valid, output reg net_tx_ready,
  input [63:0] net_tx_bits_data, input [7:0] net_tx_bits_keep,
  input net_tx_bits_last,
  output reg net_rx_valid, input net_rx_ready,
  output reg [63:0] net_rx_bits_data, output reg [7:0] net_rx_bits_keep,
  output reg net_rx_bits_last,
  input [31:0] net_txFrames, net_rxFrames, net_txBytes, net_rxBytes
);
  import "DPI-C" function void osplay_net_tick(
    input bit reset,
    input bit tx_fire, input longint unsigned tx_data,
    input byte unsigned tx_keep, input bit tx_last,
    input bit rx_fire,
    output bit rx_valid, output longint unsigned rx_data,
    output byte unsigned rx_keep, output bit rx_last);
  bit valid_next, last_next;
  longint unsigned data_next;
  byte unsigned keep_next;
  initial begin
    net_tx_ready = 0; net_rx_valid = 0;
    net_rx_bits_data = 0; net_rx_bits_keep = 0; net_rx_bits_last = 0;
  end
  always @(posedge clock) begin
    osplay_net_tick(reset, net_tx_valid && net_tx_ready,
      net_tx_bits_data, net_tx_bits_keep, net_tx_bits_last,
      net_rx_valid && net_rx_ready, valid_next, data_next, keep_next, last_next);
    net_tx_ready <= !reset;
    net_rx_valid <= valid_next;
    net_rx_bits_data <= data_next;
    net_rx_bits_keep <= keep_next;
    net_rx_bits_last <= last_next;
  end
endmodule
