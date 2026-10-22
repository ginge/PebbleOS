# Microphone integration

> Current project status (22 October 2026): [implementation, completed work and remaining tasks](current-status.md). Dated results below retain their original evidence limits.

Unhygienix uses the native T5AI audio ADC driver for 16 kHz signed 16-bit mono
capture. The board enables `CONFIG_MIC`, selecting `CONFIG_MIC_T5AI` and the
voice service. Speaker playback remains disabled.

The driver owns GDMA0 channel 1, leaving display channel 0 untouched. Two DMA
halves each hold 320 packed ADC samples (20 ms). The interrupt handler extracts
MIC1 into a 2,560-sample mono ring; KernelBG delivers completed frames to the
voice service. Startup discards settling samples before delivering audio.

The DMA buffer uses aligned internal kernel memory allocated only while
recording. The 5 KiB mono ring and a private 16 KiB voice heap reside in PSRAM.
T5AI-only build aliases route the Speex encoder and PCM allocations through
that heap, preserving shared Pebble voice code and other platforms. The heap
initializes lazily because its PSRAM section is not initialized at startup.

Start and stop serialize driver state, control audio power and interrupts, and
release the DMA allocation. A DMA bus fault stops capture and prevents another
start until reboot. Deferred callbacks tolerate a recording stopping before
they execute. Dropped-sample counters include detected overruns; late DMA laps
can make the count a lower bound.

Physical validation confirmed recording, phone transcription, and successful
completion after moving voice allocations into PSRAM. The previous internal
heap exhaustion at recording startup no longer occurred in that trial.
Continuity under sustained load remains to be qualified: the diagnostic trial
reported 4,800 dropped samples and no FIFO-full events.
