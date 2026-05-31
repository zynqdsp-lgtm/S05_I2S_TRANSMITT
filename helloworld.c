
#include "platform.h"
#include "samples.h"
#include "xaudioformatter.h"
#include "xi2stx.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xinterrupt_wrap.h"
#include "xparameters.h"
#include <stdio.h>

#define AF_FS 32 /* kHz */
#define AF_MCLK (128 * AF_FS)

#define I2S_TX_FS 32 /* kHz */
#define I2S_TX_MCLK (128 * I2S_TX_FS)

XAudioFormatterHwParams af_hw_params;
XAudioFormatter XAudioFormatterInst;
XI2s_Tx XI2sTxInst;

u32 MM2SAFIntrReceived;

void XMM2SAFCallback(void *data);

static int16_t audio_buf[256];

UINTPTR audio_buf_addr = (UINTPTR)audio_buf;
UINTPTR first_half_audio_buf_addr = (UINTPTR)audio_buf;
UINTPTR second_half_audio_buf_addr = (UINTPTR)(audio_buf + 128);

void init(void) {

  int Status;

  /********** XAudioFormatter Initialize **********/

  af_hw_params.buf_addr = audio_buf_addr;
  af_hw_params.active_ch = 2;
  af_hw_params.bits_per_sample = BIT_DEPTH_16;
  af_hw_params.periods = 2;
  af_hw_params.bytes_per_period = 256;

  Status = XAudioFormatter_Initialize(&XAudioFormatterInst,
                                      XPAR_XAUDIO_FORMATTER_0_BASEADDR);
  if (Status != XST_SUCCESS) {
    printf("ERROR: XAudioFormatter_Initialize \r\n");
  }

  XAudioFormatterInst.ChannelId = XAudioFormatter_MM2S;

  XAudioFormatter_SetMM2SCallback(&XAudioFormatterInst,
                                  XAudioFormatter_IOC_Handler, XMM2SAFCallback,
                                  (void *)&XAudioFormatterInst);

  XAudioFormatterSetFsMultiplier(&XAudioFormatterInst, AF_MCLK, AF_FS);

  XAudioFormatterSetHwParams(&XAudioFormatterInst, &af_hw_params);

  Status = XSetupInterruptSystem(
      &XAudioFormatterInst, &XAudioFormatterMM2SIntrHandler,
      XAudioFormatterInst.Config.IntrId, XAudioFormatterInst.Config.IntrParent,
      XINTERRUPT_DEFAULT_PRIORITY);
  if (Status == XST_FAILURE) {
    xil_printf("IRQ init failed.\n\r\r");
  }

  /********** XI2s_Tx Initialize **********/
  XI2stx_Config *XI2stx_ConfigPtr;

  XI2stx_ConfigPtr = XI2s_Tx_LookupConfig(XPAR_XI2STX_0_BASEADDR);
  if (XI2stx_ConfigPtr == NULL) {
    xil_printf("XI2s_Tx_LookupConfig failed! terminating\r\n");
  }

  Status = XI2s_Tx_CfgInitialize(&XI2sTxInst, XI2stx_ConfigPtr,
                                 XI2stx_ConfigPtr->BaseAddress);
  if (Status != XST_SUCCESS) {
    printf("ERROR: XI2s_Tx_CfgInitialize \r\n");
  }

  XI2s_Tx_Enable(&XI2sTxInst, 0x0);

  XI2s_Tx_SetSclkOutDiv(&XI2sTxInst, I2S_TX_MCLK, I2S_TX_FS);

  XI2s_Tx_SetChMux(&XI2sTxInst, 0, XI2S_TX_CHMUX_AXIS_01);
}

void play_start(void) {

  XAudioFormatter_InterruptEnable(&XAudioFormatterInst, XAUD_CTRL_IOC_IRQ_MASK);

  XAudioFormatterDMAStart(&XAudioFormatterInst);

  XI2s_Tx_Enable(&XI2sTxInst, 0x1);
}

void play_stop(void) {

  XAudioFormatter_InterruptDisable(&XAudioFormatterInst,
                                   XAUD_CTRL_IOC_IRQ_MASK);
  XAudioFormatterDMAStop(&XAudioFormatterInst);

  XI2s_Tx_Enable(&XI2sTxInst, 0x0);
}

void XMM2SAFCallback(void *data) {

  XAudioFormatter *AFInstancePtr = (XAudioFormatter *)data;
  XAudioFormatter_InterruptClear(AFInstancePtr, XAUD_STS_IOC_IRQ_MASK);

  MM2SAFIntrReceived = 1;
}

int main() {
  init_platform();
  Xil_DCacheEnable();

  for (int i = 0; i < 256; i++) {
    audio_buf[i] = 0;
  }

  Xil_DCacheFlushRange(audio_buf_addr, 256 * sizeof(int16_t));

  print("Start\n\r");

  init();

  play_start();

  u32 aud_idx = 0;
  u8 ping_pong = 0;

  while (aud_idx < NUM_ELEMENTS) {
    if (MM2SAFIntrReceived == 1) {
      MM2SAFIntrReceived = 0;

      if (ping_pong == 0) {
        ping_pong = 1;
        for (int i = 0; i < 64; i++) {
          audio_buf[2 * i] = data[aud_idx];
          aud_idx++;
          audio_buf[2 * i + 1] = data[aud_idx];
          aud_idx++;
        }

        Xil_DCacheFlushRange(first_half_audio_buf_addr, 128 * sizeof(int16_t));

      } else {
        ping_pong = 0;
        for (int i = 64; i < 128; i++) {
          audio_buf[2 * i] = data[aud_idx];
          aud_idx++;
          audio_buf[2 * i + 1] = data[aud_idx];
          aud_idx++;
        }

        Xil_DCacheFlushRange(second_half_audio_buf_addr, 128 * sizeof(int16_t));
      }
    }
  }

  play_stop();

  print("Done\n\r");
  cleanup_platform();
  return 0;
}
