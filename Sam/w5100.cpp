/*
 * Copyright (c) 2010 by Cristian Maglie <c.maglie@bug.st>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of either the GNU General Public License version 2
 * or the GNU Lesser General Public License version 2.1, both as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <string.h>

#include "utility/w5100.h"

// W5100 controller instance
W5100Class W5100;

#define SPI_CS 10

#define TX_RX_MAX_BUF_SIZE 2048
#define TX_BUF 0x1100
#define RX_BUF (TX_BUF + TX_RX_MAX_BUF_SIZE)

#ifdef W5200
#define TXBUF_BASE 0x8000
#define RXBUF_BASE 0xC000
#else
#define TXBUF_BASE 0x4000
#define RXBUF_BASE 0x6000
#endif

void W5100Class::init(void)
{
  delay(300);

  SPI.begin(SPI_CS);
  
  #ifdef W5200

  // more initila delay is required
  delay(200);

  // should be ok with no divider, W5200 support 80Mhz and SAM3X8E is cadenced to 84Mhz..
  // Set clock to 84Mhz but this setting NOT work: SPI clock shaping is not good.
  // SPI.setClockDivider(SPI_CS, 1); 
  
  // Set clock to 42Mhz 
  SPI.setClockDivider(SPI_CS, 2); 
 
  #else
  // Set clock to 4Mhz (W5100 should support up to 14Mhz)
  SPI.setClockDivider(SPI_CS, 21);
  #endif
  
  SPI.setDataMode(SPI_CS, SPI_MODE0);
  writeMR(1<<RST);
  
  #ifdef W5200
  for (int i=0; i<MAX_SOCK_NUM; i++) {
    write((0x4000 + i * 0x100 + 0x001F), 2);
    write((0x4000 + i * 0x100 + 0x001E), 2);
  }
  #else
  writeTMSR(0x55);
  writeRMSR(0x55);
  #endif

  for (int i=0; i<MAX_SOCK_NUM; i++) {
    SBASE[i] = TXBUF_BASE + SSIZE * i;
    RBASE[i] = RXBUF_BASE + RSIZE * i;
  }
}

uint16_t W5100Class::getTXFreeSize(SOCKET s)
{
  uint16_t val=0, val1=0;
  do {
    val1 = readSnTX_FSR(s);
    if (val1 != 0)
      val = readSnTX_FSR(s);
  } 
  while (val != val1);
  return val;
}

uint16_t W5100Class::getRXReceivedSize(SOCKET s)
{
  uint16_t val=0,val1=0;
  do {
    val1 = readSnRX_RSR(s);
    if (val1 != 0)
      val = readSnRX_RSR(s);
  } 
  while (val != val1);
  return val;
}


void W5100Class::send_data_processing(SOCKET s, const uint8_t *data, uint16_t len)
{
  // This is same as having no offset in a call to send_data_processing_offset
  send_data_processing_offset(s, 0, data, len);
}

void W5100Class::send_data_processing_offset(SOCKET s, uint16_t data_offset, const uint8_t *data, uint16_t len)
{
  uint16_t ptr = readSnTX_WR(s);
  ptr += data_offset;
  uint16_t offset = ptr & SMASK;
  uint16_t dstAddr = offset + SBASE[s];

  if (offset + len > SSIZE) 
  {
    // Wrap around circular buffer
    uint16_t size = SSIZE - offset;
    write(dstAddr, data, size);
    write(SBASE[s], data + size, len - size);
  } 
  else {
    write(dstAddr, data, len);
  }

  ptr += len;
  writeSnTX_WR(s, ptr);
}


void W5100Class::recv_data_processing(SOCKET s, uint8_t *data, uint16_t len, uint8_t peek)
{
  uint16_t ptr;
  ptr = readSnRX_RD(s);
  read_data(s, ptr, data, len);
  if (!peek)
  {
    ptr += len;
    writeSnRX_RD(s, ptr);
  }
}

void W5100Class::read_data(SOCKET s, volatile uint16_t src, volatile uint8_t *dst, uint16_t len)
{
  uint16_t size;
  uint16_t src_mask;
  uint16_t src_ptr;

  src_mask = src & RMASK;
  src_ptr = RBASE[s] + src_mask;

  if( (src_mask + len) > RSIZE ) 
  {
    size = RSIZE - src_mask;
    read(src_ptr, (uint8_t *)dst, size);
    dst += size;
    read(RBASE[s], (uint8_t *) dst, len - size);
  } 
  else
    read(src_ptr, (uint8_t *) dst, len);
}


uint8_t W5100Class::write(uint16_t _addr, uint8_t _data)
{
#ifdef W5200
  SPI.transfer(SPI_CS, _addr >> 8, SPI_CONTINUE);
  SPI.transfer(SPI_CS, _addr & 0xFF, SPI_CONTINUE);
  SPI.transfer(SPI_CS, 0x80, SPI_CONTINUE);
  SPI.transfer(SPI_CS, 0x01, SPI_CONTINUE);
#else	
  SPI.transfer(SPI_CS, 0xF0, SPI_CONTINUE);
  SPI.transfer(SPI_CS, _addr >> 8, SPI_CONTINUE);
  SPI.transfer(SPI_CS, _addr & 0xFF, SPI_CONTINUE);
#endif

  SPI.transfer(SPI_CS, _data);
  return 1;  
}

uint16_t W5100Class::write(uint16_t _addr, const uint8_t *_buf, uint16_t _len)
{
#ifdef W5200
  SPI.transfer(SPI_CS, _addr >> 8, SPI_CONTINUE);
  SPI.transfer(SPI_CS, _addr & 0xFF, SPI_CONTINUE);
  SPI.transfer(SPI_CS, (0x80 | ((_len & 0x7F00) >> 8)), SPI_CONTINUE);
  SPI.transfer(SPI_CS, _len & 0x00FF, SPI_CONTINUE);

  for (uint16_t i=0; i<_len; i++)
  {
    SPI.transfer(SPI_CS, _buf[i], SPI_CONTINUE);

  }
#else	
  for (uint16_t i=0; i<_len; i++)
  {
	SPI.transfer(SPI_CS, 0xF0, SPI_CONTINUE);
	SPI.transfer(SPI_CS, _addr >> 8, SPI_CONTINUE);
	SPI.transfer(SPI_CS, _addr & 0xFF, SPI_CONTINUE);
	SPI.transfer(SPI_CS, _buf[i]);
    _addr++;
  }
#endif
  
  return _len;
}

uint8_t W5100Class::read(uint16_t _addr)
{  
#ifdef W5200
  SPI.transfer(SPI_CS, _addr >> 8, SPI_CONTINUE);
  SPI.transfer(SPI_CS, _addr & 0xFF, SPI_CONTINUE);
  SPI.transfer(SPI_CS, 0x00, SPI_CONTINUE);
  SPI.transfer(SPI_CS, 0x01, SPI_CONTINUE);
#else
  SPI.transfer(SPI_CS, 0x0F, SPI_CONTINUE);
  SPI.transfer(SPI_CS, _addr >> 8, SPI_CONTINUE);
  SPI.transfer(SPI_CS, _addr & 0xFF, SPI_CONTINUE);
#endif
  
  uint8_t _data = SPI.transfer(SPI_CS, 0);
  return _data;
}

uint16_t W5100Class::read(uint16_t _addr, uint8_t *_buf, uint16_t _len)
{
#ifdef W5200
  SPI.transfer(SPI_CS, _addr >> 8, SPI_CONTINUE);
  SPI.transfer(SPI_CS, _addr & 0xFF, SPI_CONTINUE);
  SPI.transfer(SPI_CS, (0x00 | ((_len & 0x7F00) >> 8)), SPI_CONTINUE);
  SPI.transfer(SPI_CS, _len & 0x00FF, SPI_CONTINUE);

  for (uint16_t i=0; i<_len; i++)
  {
    _buf[i] = SPI.transfer(SPI_CS, 0, SPI_CONTINUE);

  }
#else	
  for (uint16_t i=0; i<_len; i++)
  {
	SPI.transfer(SPI_CS, 0x0F, SPI_CONTINUE);
	SPI.transfer(SPI_CS, _addr >> 8, SPI_CONTINUE);
	SPI.transfer(SPI_CS, _addr & 0xFF, SPI_CONTINUE);
    _buf[i] = SPI.transfer(SPI_CS, 0);
    _addr++;
  }
#endif  
  return _len;
}

void W5100Class::execCmdSn(SOCKET s, SockCMD _cmd) {
  // Send command to socket
  writeSnCR(s, _cmd);
  // Wait for command to complete
  while (readSnCR(s))
    ;
}
