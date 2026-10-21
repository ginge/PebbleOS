# SPDX-License-Identifier: Apache-2.0
import unittest


class DmaIrqTest(unittest.TestCase):
    def test_completion_races_and_lost_irq(self):
        import re
        import subprocess
        import tempfile
        from pathlib import Path

        s = (
            Path(__file__).resolve().parents[2]
            / "drivers/display/t5ai/display_co5300.c"
        ).read_text()

        def fn(name):
            m = re.search(
                r"^(?:static )?(?:bool|void) " + name + r"\(", s, re.MULTILINE
            )
            a = s.index("{", m.end())
            b = a + 1
            d = 1
            while d:
                d += (s[b] == "{") - (s[b] == "}")
                b += 1
            return s[m.start() : b]

        prefix = r"""
        #include <assert.h>
        #include <stdint.h>
        #include <stdbool.h>
        #include <stddef.h>
        #define CONFIG_T5AI_DISPLAY_DMA_IRQ 1
        #define DISPLAY_DMA_ENABLE 1u
        #define DISPLAY_DMA_STATUS_BUS_ERROR (1u<<20)
        #define DISPLAY_DMA_STATUS_CLEAR ((1u<<18)|(1u<<19)|(1u<<20))
        #define DISPLAY_DMA_TIMEOUT_TICKS 640u
        #define PBL_NO_WAIT 0
        #define PBL_MSEC(x) x
        #define TRANSFER_BEGIN(x)
        static uint32_t DMA_CTRL,DMA_STATUS,s_dma_started_ticks,ticks,step,finish_at;
        static volatile bool s_dma_pending;
        static volatile uint32_t s_dma_irq_status;
        static int s_dma_done, mode, waits,signals;
        void display_co5300_dma_irq(void);
        static void pbl_sem_give(int *s) {++signals;*s=1;}
        static int pbl_sem_take(int *s,int timeout) {
         (void)timeout;++waits;
         if(!(DMA_CTRL&1u) && mode==1) {display_co5300_dma_irq();DMA_STATUS=0;}
         if(*s){*s=0;return 0;}ticks+=20;return -1;
        }
        static uint32_t now(void) {
         ticks+=step;
         if(finish_at && (uint32_t)(ticks-s_dma_started_ticks)>=finish_at) {DMA_CTRL=0;DMA_STATUS|=1u<<18;}
         return ticks;
        }
        #define T5AI_AON_RTC_COUNT_LOW now()
        """
        finish = re.sub(
            r"#ifdef CONFIG_T5AI_DISPLAY_PROFILE.*?#endif",
            "",
            fn("prv_pixel_stream_finish_dma"),
            flags=re.DOTALL,
        )
        main = r"""
        static void reset(void) {DMA_CTRL=1;DMA_STATUS=0;s_dma_started_ticks=ticks=0;step=1;finish_at=10;s_dma_pending=true;s_dma_irq_status=0;s_dma_done=signals=waits=0;mode=1;}
        int main(void) {
         size_t a=0;reset();assert(prv_pixel_stream_finish_dma(&a,932) && a==233 && !s_dma_pending && !DMA_CTRL);
         // Completion before the waiter: the IRQ's cleared status must remain visible.
         reset();DMA_CTRL=0;DMA_STATUS=1u<<20;display_co5300_dma_irq();DMA_STATUS=0;
         a=0;assert(!prv_pixel_stream_finish_dma(&a,932) && !a && signals==1);
         reset();DMA_CTRL=0;DMA_STATUS=1u<<18;display_co5300_dma_irq();DMA_STATUS=0;
         a=0;assert(prv_pixel_stream_finish_dma(&a,932) && a==233);
         // A lost IRQ is bounded by elapsed time even without semaphore signals.
         reset();finish_at=0;mode=0;a=0;assert(!prv_pixel_stream_finish_dma(&a,932) && waits<40 && !DMA_CTRL);
         reset();s_dma_started_ticks=ticks=UINT32_MAX-5;a=0;assert(prv_pixel_stream_finish_dma(&a,932));
         // Microphone-only IRQ cannot consume channel-zero status when it has no owner.
         reset();s_dma_pending=false;DMA_STATUS=1u<<18;display_co5300_dma_irq();assert(!signals && DMA_STATUS==(1u<<18));
         return 0;
        }
        """
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "test.c").write_text(
                prefix + fn("display_co5300_dma_irq") + finish + main
            )
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined",
                    "-fno-sanitize-recover=all",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
