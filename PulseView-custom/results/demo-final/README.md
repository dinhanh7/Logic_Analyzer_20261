# Final default demo

Command: `DISPLAY=:0 bash scripts/demo.sh` (no extra options).

Result PASS/PASS_LIVE. The script closed only its previous demo process,
verified streaming+four protocols, captured through rp2350-la, checked real TFT
bytes/ACK/framebuffer, then captured and decoded inside PulseView.
Default TFT dwell is2seconds per screen. PulseView was left open.

Original output: /home/namkhanh/EmbeddedSystem/Code/Logic_Analyzer_20261/PulseView-custom/build/demo/20261118-021651
