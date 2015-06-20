
#include "mbed.h"
#include "mcp23s08.h"

#define INPUT 0
#define OUTPUT 1

#define LOW 0
#define HIGH 1

mcp23s08::mcp23s08(PinName mosi, PinName miso, PinName clk, PinName cs_pin,const uint8_t haenAdrs) : SPI(mosi, miso, clk), cs(cs_pin) {
	format(8, 3);
	frequency(2000000);

	postSetup(haenAdrs);

}


void mcp23s08::postSetup(const uint8_t haenAdrs){
	if (haenAdrs >= 0x20 && haenAdrs <= 0x23){//HAEN works between 0x20...0x23
		_adrs = haenAdrs;
		_useHaen = 1;
	} else {
		_adrs = 0;
		_useHaen = 0;
	}
	_readCmd =  (_adrs << 1) | 1;
	_writeCmd = _adrs << 1;
	//setup register values for this chip
	IOCON = 	0x05;
	IODIR = 	0x00;
	GPPU = 		0x06;
	GPIO = 		0x09;
	GPINTEN = 	0x02;
	IPOL = 		0x01;
	DEFVAL = 	0x03;
	INTF = 		0x07;
	INTCAP = 	0x08;
	OLAT = 		0x0A;
	INTCON = 	0x04;
}

void mcp23s08::begin(bool protocolInitOverride) {
	
	cs=1;
	wait(0.1);
	_useHaen == 1 ? writeByte(IOCON,0b00101000) : writeByte(IOCON,0b00100000);
	/*
    if (_useHaen){
		writeByte(IOCON,0b00101000);//read datasheet for details!
	} else {
		writeByte(IOCON,0b00100000);
	}
	*/
	_gpioDirection = 0xFF;//all in
	_gpioState = 0x00;//all low 
}


uint8_t mcp23s08::readAddress(uint8_t addr){
	uint8_t low_byte = 0x00;
	startSend(1);
	SPI::write(addr);
	low_byte = (uint8_t)SPI::write(0x00);
	endSend();
	return low_byte;
}



void mcp23s08::gpioPinMode(uint8_t mode){
	if (mode == INPUT){
		_gpioDirection = 0xFF;
	} else if (mode == OUTPUT){	
		_gpioDirection = 0x00;
		_gpioState = 0x00;
	} else {
		_gpioDirection = mode;
	}
	writeByte(IODIR,_gpioDirection);
}

void mcp23s08::gpioPinMode(uint8_t pin, bool mode){
	if (pin < 8){//0...7
		mode == INPUT ? _gpioDirection |= (1 << pin) :_gpioDirection &= ~(1 << pin);
		writeByte(IODIR,_gpioDirection);
	}
}

void mcp23s08::gpioPort(uint8_t value){
	if (value == HIGH){
		_gpioState = 0xFF;
	} else if (value == LOW){	
		_gpioState = 0x00;
	} else {
		_gpioState = value;
	}
	writeByte(GPIO,_gpioState);
}


uint8_t mcp23s08::readGpioPort(){
	return readAddress(GPIO);
}

uint8_t mcp23s08::readGpioPortFast(){
	return _gpioState;
}

int mcp23s08::gpioDigitalReadFast(uint8_t pin){
	if (pin < 8){//0...7
		int temp = _gpioState & (1 << pin);
		return temp;
	} else {
		return 0;
	}
}

void mcp23s08::portPullup(uint8_t data) {
	if (data == HIGH){
		_gpioState = 0xFF;
	} else if (data == LOW){	
		_gpioState = 0x00;
	} else {
		_gpioState = data;
	}
	writeByte(GPPU, _gpioState);
}




void mcp23s08::gpioDigitalWrite(uint8_t pin, bool value){
	if (pin < 8){//0...7
		value == HIGH ? _gpioState |= (1 << pin) : _gpioState &= ~(1 << pin);
		writeByte(GPIO,_gpioState);
	}
}

void mcp23s08::gpioDigitalWriteFast(uint8_t pin, bool value){
	if (pin < 8){//0...8
		value == HIGH ? _gpioState |= (1 << pin) : _gpioState &= ~(1 << pin);
	}
}

void mcp23s08::gpioPortUpdate(){
	writeByte(GPIO,_gpioState);
}

int mcp23s08::gpioDigitalRead(uint8_t pin){
	if (pin < 8) return (int)(readAddress(GPIO) & 1 << pin);
	return 0;
}

uint8_t mcp23s08::gpioRegisterReadByte(uint8_t reg){
  uint8_t data = 0;
  startSend(1);
  SPI::write(reg);
  data = (uint8_t)SPI::write(0x00);
  endSend();
  return data;
}


void mcp23s08::gpioRegisterWriteByte(uint8_t reg,uint8_t data){
	writeByte(reg,(uint8_t)data);
}

/* ------------------------------ Low Level ----------------*/
void mcp23s08::startSend(bool mode){
	cs=0;
	mode == 1 ? SPI::write(_readCmd) : SPI::write(_writeCmd);
}

void mcp23s08::endSend(){
	cs=1;
}


void mcp23s08::writeByte(uint8_t addr, uint8_t data){
	startSend(0);
	SPI::write(addr);
	SPI::write(data);
	endSend();
}


