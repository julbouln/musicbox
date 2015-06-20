#ifndef _SAM2695_H_
#define _SAM2695_H_

class Sam2695 {
	Serial midi_in;
	DigitalOut rst;

public:
	Sam2695(PinName midi_in_pin, PinName rst_pin) : midi_in(midi_in_pin, NC), rst(rst_pin) {
		midi_in.baud(31250);
	};

	void reset() {
		rst = 0;
		wait_ms(10);
		rst = 1;
		wait_ms(100);
	}

	void shutdown() {
		rst = 0;
	}

	void send(uint8_t c) {
		midi_in.putc(c);
	}

	void setVolume(uint8_t volume) {
		midi_in.putc(0xF0);
		midi_in.putc(0x7F);
		midi_in.putc(0x7F);
		midi_in.putc(0x04);
		midi_in.putc(0x01);
		midi_in.putc(0x00);
		midi_in.putc(volume);
		midi_in.putc(0xF7);
	}

	void selectBank(uint8_t channel, uint8_t bank) {
		midi_in.putc(0xB0 + channel);
		midi_in.putc(0x00);
		midi_in.putc(bank);
	}

};
#endif // _SAM2695_H_