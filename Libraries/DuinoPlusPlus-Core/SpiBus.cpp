/*
 * MIT License
 *
 * Copyright (c) 2016 Antoine de Maleprade
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "SpiBus.h"

SpiBus::SpiBus(int arduinoPin) :
  csPort(0),
  csDdr(0),
  csPin(0),
  csValide(false),
  invertRW(false)
  {
    uint8_t port = digitalPinToPort(arduinoPin);
    if(port != NOT_A_PIN) {
      csPort = portOutputRegister(port);
      csDdr = portModeRegister(port);
      uint8_t mask = digitalPinToBitMask(arduinoPin);
      csPin = 0;
      while(!((mask) & 1)) {
	mask >>= 1;
	csPin++;
      }
      csValide = true;
    }
  }

SpiBus::SpiBus(volatile uint8_t *newCsPort,
  uint8_t newCsPin, bool newInvertRW) :
  csPort(newCsPort),
  csDdr(newCsPort-1),
  csPin(newCsPin),
  csValide(false),
  invertRW(newInvertRW)
  {
    csValide = checkCs();
  }

  void SpiBus::printError(const String errorStr)
  {
#ifdef ERROR_PRINT
    String errorMessage("spi:err:");
    errorMessage += errorStr;
    ERROR_PRINT(errorMessage);
#else
    (void) errorStr;
#endif
  }

  void SpiBus::begin()
  {
    if(!csValide)
    {
      printError("badCs");
      return;
    }
    csConfigure();
    init();
  }

  void SpiBus::init()
  {
    static bool isInit = false;
    if(isInit) return;
    #if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega16U4__) || defined(__AVR_ATmega32U4__)
    DDRB |= (1<<DDB1)|(1<<DDB2); // MOSI & CLK output
    #elif defined(__AVR_ATmega48A__) || defined(__AVR_ATmega48PA__) || defined(__AVR_ATmega88A__) || defined(__AVR_ATmega88PA__) || defined(__AVR_ATmega168A__) || defined(__AVR_ATmega168PA__) || defined(__AVR_ATmega328__) || defined(__AVR_ATmega328P__)
    DDRB |= (1<<DDB3)|(1<<DDB5);
    #else
    #error "MISO,MOSI&CLK configuration not defined for this processor"
    #endif

    int divider = F_CPU/SPI_SPEED/2;
    int dividerPower = -1;
    for (int i = 7; i>= 0; i--)
    {
      if((dividerPower == -1) && (divider & (1<<i)))
      {
        dividerPower = i;
      }else if(divider & (1<<i)){
        dividerPower++;
        break;
      }
    }
    bool spi2x;
    uint8_t spi_spr;
    if(dividerPower>=7)
    {
      spi_spr = 0b11;
      spi2x = false;
    }else{
      spi2x = ((dividerPower&0x01)==0x01);
      if(spi2x)
      {
        spi_spr = (dividerPower-1)/2;
      }else{
        spi_spr = (dividerPower)/2-1;
      }
    }
    SPCR = (1 << SPE)
    | ((SPI_LSB_FIRST&0x01)<<DORD)        // Data order
    | (1<<MSTR)                  // Master Spi mode
    | ((SPI_LEADING_FALLING&0x01)<<CPOL) // Clock polarity
    | ((SPI_LEADING_SETUP&0x01)<<CPHA) // Clock Phase
    | (spi_spr&0b11);            // Clock divider
    if(spi2x)
    {
      SPSR |= (1<<SPI2X);
    }else{
      SPSR &= ~(1<<SPI2X);
    }

    isInit = true;
  }

  bool SpiBus::isConnected()
  {
    return csValide;
  }

  bool SpiBus::cmd(uint8_t cmd)
  {
    select();
    uint8_t data;
    data = cmd; // write
    if(!exchangeByte(&data)){unselect(); return false;}
    unselect();
    return true;
  }

  bool SpiBus::write(uint8_t reg, uint8_t val)
  {
    return write(reg, &val, 1);
  }

  uint8_t SpiBus::read(uint8_t reg)
  {
    uint8_t readValue;
    if(read(reg, &readValue, 1))
    {
      return readValue;
    }else{
      return -1;
    }
  }

  bool SpiBus::write(uint8_t reg, uint8_t *buffer, size_t len)
  {
    select();
    uint8_t data;
    if (invertRW){
      data = (1<<7)|reg; // write
    } else {
      data = (~(1<<7))&reg; // write
    }
    if(!exchangeByte(&data)){unselect(); return false;}
    for(unsigned int i=0; i<len; i++)
    {
      data = buffer[i];
      if(!exchangeByte(&data)){unselect(); return false;}
    }
    unselect();
    return true;
  }

  bool SpiBus::read(uint8_t reg, uint8_t *buffer, size_t len)
  {
    if (invertRW){
      return cmdRead((~(1<<7))&reg, buffer, len); // read
    } else {
      return cmdRead((1<<7)|reg, buffer, len); // read
    }
  }

  bool SpiBus::cmdRead(uint8_t cmd, uint8_t *buffer, size_t len)
  {
    select();
    uint8_t data;
    data = cmd;
    if(!exchangeByte(&data)){unselect(); return false;}
    for(size_t i=0; i<len; i++)
    {
      delayMicroseconds(100);
      data = 0;
      if(!exchangeByte(&data)){unselect(); return false;}
      buffer[i] = data;
    }
    delayMicroseconds(1);
    unselect();
    return true;
  }

  bool SpiBus::exchangeByte(uint8_t *val)
  {
    SPDR = *val;
    bool success = waitTimeout();
    *val = SPDR;
    return success;
  }

  bool SpiBus::waitTimeout()
  {
    unsigned long startTime = micros();
    do
    {
      uint8_t spsrTemp = (SPSR&0xC0);
      if(spsrTemp&(1<<WCOL))
      {
        return false;
      }else if(spsrTemp&(1<<SPIF))
      {
        return true;
      }
    }while((micros()-startTime)<SPI_TIMEOUT_US);
    printError("timeout");
    return false;
  }

  bool SpiBus::csConfigure()
  {
    if(csValide)
    {
      (*csDdr) |= (1<<csPin);
      unselect();
      return true;
    }
    return false;
  }

  void SpiBus::select()
  {
    if(csValide)
    *csPort &= ~(1<<csPin);
  }

  void SpiBus::unselect()
  {
    if(csValide)
    *csPort |= (1<<csPin);
  }

  bool SpiBus::checkCs()
  {
    bool portOk = false;
    #ifdef PORTA
    if((csPort == &PORTA) && (csDdr == &DDRA)) portOk = true;
    #endif
    #ifdef PORTB
    if((csPort == &PORTB) && (csDdr == &DDRB)) {portOk = true;}
    #endif
    #ifdef PORTC
    if((csPort == &PORTC) && (csDdr == &DDRC)) portOk = true;
    #endif
    #ifdef PORTD
    if((csPort == &PORTD) && (csDdr == &DDRD)) portOk = true;
    #endif
    #ifdef PORTE
    if((csPort == &PORTE) && (csDdr == &DDRE)) portOk = true;
    #endif
    #ifdef PORTF
    if((csPort == &PORTF) && (csDdr == &DDRF)) portOk = true;
    #endif
    #ifdef PORTG
    if((csPort == &PORTG) && (csDdr == &DDRG)) portOk = true;
    #endif
    #ifdef PORTH
    if((csPort == &PORTH) && (csDdr == &DDRH)) portOk = true;
    #endif
    return portOk && (csPin < 8);
  }
