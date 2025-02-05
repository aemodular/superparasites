// Copyright 2014 Olivier Gillet.
// 
// Author: Olivier Gillet (ol.gillet@gmail.com)
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.

#include "supercell/cv_scaler.h"
#include "supercell/drivers/codec.h"
#include "supercell/drivers/debug_pin.h"
#include "supercell/drivers/debug_port.h"
#include "supercell/drivers/system.h"
#include "supercell/drivers/version.h"
#include "supercell/dsp/granular_processor.h"
#include "supercell/meter.h"
#include "supercell/resources.h"
#include "supercell/settings.h"
#include "supercell/ui.h"

// #define PROFILE_INTERRUPT 1

using namespace clouds;
using namespace stmlib;

GranularProcessor processor;
Codec codec;
DebugPort debug_port;
CvScaler cv_scaler;
//Meter meter;
Meter in_meter;
Meter out_meter;
Settings settings;
Ui ui;

// Pre-allocate big blocks in main memory and CCM. No malloc here.
uint8_t block_mem[118528];      //  ### MB 20220109 reduced by 256 bytes from 118784 for new variables needed for Cirrus random pitch issue, otherwise the sound-mode "Spectral Clouds" crashes when feedback is high
uint8_t block_ccm[65536 - 128] __attribute__ ((section (".ccmdata")));
//uint8_t block_ccm[65536 - 128] __attribute__ ((section (".ccmram")));

int __errno;
void operator delete(void * p) { }

// Default interrupt handlers.
extern "C" {

void NMI_Handler() { }
void HardFault_Handler() { while (1); }
void MemManage_Handler() { while (1); }
void BusFault_Handler() { while (1); }
void UsageFault_Handler() { while (1); }
void SVC_Handler() { }
void DebugMon_Handler() { }
void PendSV_Handler() { }
void __cxa_pure_virtual() { while (1); }

}

extern "C" {

void SysTick_Handler() {
  ui.Poll();
  if (settings.freshly_baked()) {
    if (debug_port.readable()) {
      uint8_t command = debug_port.Read();
      uint8_t response = ui.HandleFactoryTestingRequest(command);
      debug_port.Write(response);
    }
  }
}

}

void FillBuffer(Codec::Frame* input, Codec::Frame* output, size_t n) {
#ifdef PROFILE_INTERRUPT
  TIC
#endif  // PROFILE_INTERRUPT
  cv_scaler.Read(processor.mutable_parameters());
  in_meter.Process(input, n); // Process input meter before processing (avoids mutes, etc.)
  processor.Process((ShortFrame*)input, (ShortFrame*)output, n);
  out_meter.Process(output, n);
  //meter.Process(processor.parameters().freeze ? output : input, n);
#ifdef PROFILE_INTERRUPT
  TOC
#endif  // PROFILE_INTERRUPT
}

void Init() {
  System sys;
  Version version;

  sys.Init(true);
  version.Init();

  // Init granular processor.
  processor.Init(
      block_mem, sizeof(block_mem),
      block_ccm, sizeof(block_ccm));

  settings.Init();
  cv_scaler.Init(settings.mutable_calibration_data());
  //meter.Init(32000);
  in_meter.Init(32000);
  out_meter.Init(32000);
  ui.Init(&settings, &cv_scaler, &processor, &in_meter, &out_meter);

  bool master = !version.revised();
  if (!codec.Init(master, 32000)) {
    ui.Panic();
  }
  if (!codec.Start(32, &FillBuffer)) {
    ui.Panic();
  }
  if (settings.freshly_baked()) {
#ifdef PROFILE_INTERRUPT
    DebugPin::Init();
#else
    debug_port.Init();
#endif  // PROFILE_INTERRUPT
  }


  sys.StartTimers();
}

int main(void) {
  Init();
  while (1) {
    ui.DoEvents();
    processor.Prepare();
  }
}
