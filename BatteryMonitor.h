
/*
CHARGE CYCLE STATE			STAT1 	STAT2 	PG
Shutdown (V DD = V BAT ) 	Hi-Z 	Hi-Z 	Hi-Z	111
Shutdown (V DD = IN) 		Hi-Z 	Hi-Z 	L 		110
Preconditioning 			L 		Hi-Z 	L 		010
Constant Current 			L 		Hi-Z 	L 		010
Constant Voltage 			L 		Hi-Z 	L 		010
Charge Complete - Standby 	Hi-Z 	L 		L 		100
Temperature Fault 			L 		L 		L 		000
Timer Fault 				L 		L 		L 		000
Low Battery Output 			L 		Hi-Z 	Hi-Z 	011
No Battery Present 			Hi-Z 	Hi-Z 	L 		110
No Input Power Present 		Hi-Z 	Hi-Z 	Hi-Z 	111
*/

#ifndef _BATTERYMONITOR_H_
#define _BATTERYMONITOR_H_

#define BAT_STATE_FAULT	0b000
#define	BAT_STATE_CHARGE 0b010
#define	BAT_STATE_LOW 0b011
#define	BAT_STATE_COMPLETE 0b100
#define	BAT_STATE_NO_BATTERY 0b110
#define BAT_STATE_NO_POWER 0b111

class BatteryMonitor {
	AnalogIn bat_sense;
	DigitalIn bat_stat1;
	DigitalIn bat_stat2;
	DigitalIn bat_pg;

public:
	BatteryMonitor(PinName bat_sense_pin, PinName bat_stat1_pin, PinName bat_stat2_pin, PinName bat_pg_pin)
		: bat_sense(bat_sense_pin), bat_stat1(bat_stat1_pin), bat_stat2(bat_stat2_pin), bat_pg(bat_pg_pin) {
		bat_stat1.mode(PullUp);
		bat_stat2.mode(PullUp);
		bat_pg.mode(PullUp);
	}

	int state() {
		return ((bat_stat1 << 2) + (bat_stat2 << 1) + bat_pg);
	}

	float senseRead() {
		return (bat_sense.read());
	}

	float voltage() {
		return ((bat_sense.read() * 3.3) / 0.32);
	}

	int percent() {
		int sense_samples = 512;
		float v = 0;
		for (int i = 0; i < sense_samples; i++) {
			v += voltage();
		}
		int p = (v / sense_samples * 1000.0 - 3100.0) / (4200.0 - 3100.0) * 100;
		if (p < 0)
			p = 0;
		if (p > 100)
			p = 100;
		return p;
	}

};

#endif // _BATTERYMONITOR_H_