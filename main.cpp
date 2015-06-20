/*
	musicbox firmware
*/
#include "mbed.h"
#include "rtos.h"
#include "CcgMusic.h"

#include "Adafruit_SSD1306.h"
#include "string"

#include "USBMIDI.h"
//#include "USBSerial.h"

#include "SDFileSystem.h"
#include "MD_MIDIFile.h"

#include "Rng.h"
#include "Sam2695.h"
#include "mcp23s08.h"
#include "BatteryMonitor.h"

/* Board pins */
#define SCREEN_MOSI PA_7
#define SCREEN_SCK	PA_5
#define SCREEN_DC	PC_5
#define SCREEN_RST	PC_4
#define SCREEN_CS	PB_0

#define PAD_MISO PA_6
#define PAD_CS PB_1
#define PAD_INT PA_0

#define MIDI_RST PC_11
#define MIDI_OUT PC_10

#define JACK_SENSE PB_12
#define AMP_SHDN PB_13

#define BAT_SENSE PC_0
#define BAT_STAT1 PB_3
#define BAT_STAT2 PB_4
#define BAT_PG PB_5

#define SD_CS PC_1
#define SD_MISO PC_2
#define SD_MOSI PC_3
#define SD_SCK PB_10
#define SD_CD PC_15

/* PAD */
#define PAD_TOP 0b10111
#define PAD_BOTTOM 0b11110
#define PAD_PREVIOUS 0b11101
#define PAD_NEXT 0b01111
#define PAD_SELECT 0b11011

/* Globals */

const unsigned char usb_icon_bits[] = {
	0x00, 0x00, 0x03, 0x00, 0x00, 0xf8, 0x07, 0x00, 0x00, 0x0c, 0x03, 0x00,
	0x0c, 0x06, 0x00, 0x00, 0x1e, 0x03, 0x80, 0x01, 0xff, 0xff, 0xff, 0x07,
	0xff, 0xff, 0xff, 0x07, 0x1e, 0x10, 0x80, 0x01, 0x0c, 0x60, 0x00, 0x00,
	0x00, 0xc0, 0x1c, 0x00, 0x00, 0x80, 0x1f, 0x00, 0x00, 0x00, 0x1c, 0x00
};

mcp23s08 pad(SCREEN_MOSI, PAD_MISO, SCREEN_SCK, PAD_CS, 0x20);

Rng rng;

USBMIDI midiUsb;

InterruptIn jackSense(JACK_SENSE);
DigitalOut shdnAmp(AMP_SHDN);

InterruptIn padInt(PAD_INT);

Sam2695 midiOut(MIDI_OUT, MIDI_RST);

//SDFileSystem sd(SD_MOSI, SD_MISO, SD_SCK, SD_CS, "sd"); 
SDFileSystem sd(SD_MOSI, SD_MISO, SD_SCK, SD_CS, "sd", SD_CD, SDFileSystem::SWITCH_NONE,25000000); 
//osThreadId mainThreadID;

// an SPI sub-class that provides a constructed default
class SPIPreInit : public SPI
{
public:
	SPIPreInit(PinName mosi, PinName miso, PinName clk) : SPI(mosi, miso, clk)
	{
		format(8, 3);
		frequency(2000000);
	};
};

SPIPreInit screenSpi(SCREEN_MOSI, NC, SCREEN_SCK);
Adafruit_SSD1306_Spi screen(screenSpi, SCREEN_DC, SCREEN_RST, SCREEN_CS, 64, 128);

BatteryMonitor batteryMonitor(BAT_SENSE, BAT_STAT1, BAT_STAT2, BAT_PG);


class MidiRaw : public MidiDriver {
	Mutex mutex;
public:
	MidiRaw() {
		min_queue_size = 100;
		max_queue_size = 200;

	}
	void mutexLock() {
		mutex.lock();
	}
	void mutexUnlock() {
		mutex.unlock();
	}
	void msleep(int ms) {
		Thread::wait(ms * 4);
	}

	void sendMessage(QueueMessage *qm)
	{
		uint8_t *m = qm->getMessage();
		for (int i = 0; i < qm->getSize(); i++)
			midiOut.send(m[i]);

//		printf("MidiMessage: %x %x %x (%d)\n",m[0],m[1],m[2],qm->getSize());

		if (midiUsb.suspended)
			midiUsb.write(MIDIMessage(m, qm->getSize()));
	}
};

MidiRaw midiWriter;

int low_battery = 0;

/* Callbacks & interrupts */

void jackPlugged() {
	shdnAmp = 0;
}

void jackUnplugged() {
	shdnAmp = 1;
}

void transmitMessage(MIDIMessage msg) {
	for (int i = 1; i < msg.length; i++) {
		midiOut.send(msg.data[i]);
	}
}

void processMidiDriver(void const *args) {
	while (1) {
		midiWriter.launch();
//		osSignalSet(mainThreadID, 0x1);
		Thread::wait(100);

	}
}

// external clock on PA_8
// crystal should be 12mhz
void setClockOutput()
{
	GPIO_InitTypeDef InitStruct;

	HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_1);   // select PLLCLK as source

	//Configure GPIO pin to output clock signal at  PA8
	//  (1 << (PA_8 & 0xF)) == GPIO_PIN_8
	InitStruct.Pin = GPIO_PIN_8;
	InitStruct.Mode = GPIO_MODE_AF_PP;
	InitStruct.Pull = GPIO_NOPULL;
	InitStruct.Speed = GPIO_SPEED_HIGH;
	InitStruct.Alternate = GPIO_AF0_MCO;
	HAL_GPIO_Init(GPIOA, &InitStruct);
}


void renderBattery(int x, int y, int percent)
{
	screen.drawRect(x, y, 10, 6, WHITE);
	screen.fillRect(x + 11, y + 2, 2, 2, WHITE);
	screen.fillRect(x, y, (percent) / 10, 6, WHITE);
}

void checkJackSense() {
	if (jackSense) {
		shdnAmp = 0;
	} else {
		shdnAmp = 1;
	}
}

void shutdownSound() {
	midiOut.shutdown();
	shdnAmp = 0;
}

void activateSound() {
	midiOut.reset();
	checkJackSense();
}

void initJackSense() {
	shdnAmp = 0;

//	checkJackSense();

	jackSense.rise(&jackPlugged);
	jackSense.fall(&jackUnplugged);
}


class Program {
public:
	bool terminate;
	bool isPlaying;


	Program() {
		terminate = false;
	}

	virtual void init() {}

	virtual void process() {}

	virtual void parsePad(int padio) {}

	virtual void updateScreen() {}

	virtual string name() {
		return "";
	}

protected:

	void play() {
		isPlaying = true;
		midiWriter.setPause(false);
		midiWriter.setStop(false);
	}

	void pause() {
		isPlaying = false;
		midiWriter.setPause(true);
	}

	void stop() {
		isPlaying = false;
		midiWriter.setStop(true);
	}

};

typedef Program *(*ProgramCreator)();

template <typename T>
static Program *makeProgram()
{
	return new T {};
};


Program *program;
string nextProgram;

uint8_t pad_gpio = 0;
uint8_t pad_pressed = 0;

Mutex screenMutex;

void padPressed() {
	pad_pressed++;
}

void padReleased() {
	pad_pressed = 0;
}

void initPad() {
	pad.begin();
	pad.gpioRegisterWriteByte(pad.IOCON, 0b00100000); //chip configure,serial, HAEN, etc.
	pad.gpioRegisterWriteByte(pad.IODIR, 0b00011111); //pin0..4(in) / 5...7(out)
	pad.gpioRegisterWriteByte(pad.GPPU, 0b00011111); //pull up resistors
	pad.gpioRegisterWriteByte(pad.DEFVAL, 0b00011111); //default comparison value for pin 0..4
	pad.gpioRegisterWriteByte(pad.GPINTEN, 0b00011111); //int on change enable on pin 0..4
	pad.gpioRegisterWriteByte(pad.INTCON, 0b00011111); //int on change control on pin 0..4

	pad.gpioRegisterReadByte(pad.INTCAP);//clear int register

	padInt.fall(&padPressed);
	padInt.rise(&padReleased);

}

void processInterface(void const *args) {
	while (1) {
		screenMutex.lock();
		/*
				if (low_battery) {
					midiOut.shutdown();
					shdnAmp = 0;
				}
		*/

		if (!program->terminate) {
			if (pad_pressed > 0) {
				padInt.disable_irq();
				pad_gpio = pad.gpioRegisterReadByte(pad.GPIO);

				// only call parsePad once per press
				if (pad_pressed == 1)
					program->parsePad(pad_gpio);

				padInt.enable_irq();
			}
		}

		screen.fillScreen(BLACK);
		screen.setTextCursor(0, 0);

		if (!program->terminate) {
			screen.printf("%s\r\n", program->name().c_str());
			program->updateScreen();
		}

		renderBattery(128 - 12, 0, batteryMonitor.percent());

		if (midiUsb.suspended) {
			screen.drawXBitmap(128 - 27, 14, usb_icon_bits, 27, 12, WHITE);
		}
/*
		switch (batteryMonitor.state()) {
		case BAT_STATE_FAULT:
			low_battery = 0;
			screen.printf("BAT_STATE_FAULT\r\n");
			break;
		case BAT_STATE_CHARGE:
			low_battery = 0;
			screen.printf("BAT_STATE_CHARGE\r\n");
			break;
		case BAT_STATE_LOW:
			low_battery = 1;
			screen.printf("BAT_STATE_LOW\r\n");
			break;
		case BAT_STATE_COMPLETE:
			low_battery = 0;
			screen.printf("BAT_STATE_COMPLETE\r\n");
			break;
		case BAT_STATE_NO_BATTERY:
			low_battery = 0;
			screen.printf("BAT_STATE_NO_BATTERY\r\n");
			break;
		case BAT_STATE_NO_POWER:
			low_battery = 0;
			screen.printf("BAT_STATE_NO_POWER\r\n");
			break;

		}
*/
//			screen.printf("VBAT %f\r",batteryMonitor.voltage());
//		screen.printf("VBAT %f\r\n", batteryMonitor.voltage());
//		screen.printf("VBAT %d\r\n", batteryMonitor.percent());

//		screen.printf("PAD %d %d %d\r\n", pad_gpio, pad_pressed,program->terminate);
//		screen.printf("MIDI %d\r", midiWriter.getQueueSize());

		screen.display();
		screenMutex.unlock();

//		wait(0.1);
		Thread::wait(200);
	}
}

//#define PROGRAM_COUNT 5
//Program *programs[PROGRAM_COUNT];
map<string, ProgramCreator> programsMap;
vector<string> programs;


// Ccgmusic program

#define ARRANGEMENTS_COUNT 11
#define STRUCTURES_COUNT 2
#define TEMPO_COUNT 8

class CcgProgram : public Program {
	string structureScript;
	string arrangementScript;
	int tempo;
	int seed;

	const string arrangements[ARRANGEMENTS_COUNT] {
		"Piano Simple Arrangement",
		"Piano Advanced Swinging Blues",
		"Piano Advanced Boogie Woogie",
		"Piano Advanced Disco",
		"Piano Advanced Classical",
		"Simple Latin Style Arrangement", 
		"Simple Dance Style Arrangement",
		"Simple Instrumental March Arrangement",
		"Simple Ballad Style Arrangement", 
		"Simple Punk Rock Style Arrangement",
		"Random Electro Rock"
	};
	const string structures[STRUCTURES_COUNT] = {
//		"Classical Structure Small",
		"Classical Structure Big",
		"Modern Song Structure"
//		"Random Structure"
	};
	const int tempos[TEMPO_COUNT] = {60, 80, 100, 120, 160, 180, 200, 240};

	int currentSel;

public:
	CcgProgram() : Program() {
		structureScript = "Classical Structure Big";
		arrangementScript = "Piano Advanced Classical";
		tempo = 120;

		setRandomSeed();
		isPlaying = true;

		currentSel = 0;

	}

	void process() {
		activateSound();

		currentSel = 0;

		play();
		while (!terminate) {
			if (isPlaying) {
				midiWriter.setStop(false);

				SongCreator *songCreator = new SongCreator();
				songCreator->createSong(seed, tempo, structureScript, arrangementScript, &midiWriter);
//				osSignalWait(0x1, osWaitForever);
				while (midiWriter.getQueueSize() > 0) {
					wait_ms(100);
				}
				delete songCreator;

				setRandomSeed();

			} else {
				wait_ms(100);
			}
		}
	}

	void parsePad(int pad_gpio) {

		if (pad_gpio == PAD_PREVIOUS) {
			if (currentSel > 0)
				currentSel--;
		}

		if (pad_gpio == PAD_NEXT) {
			if (currentSel < 2)
				currentSel++;
		}

		// select
		if (pad_gpio == PAD_SELECT) {
			switch (currentSel) {
			case 0:
				if (isPlaying) {
					pause();
				}
				else {
					play();
				}
				break;
			case 1:
				nextSeed();
				break;
			case 2:
				stop();
				shutdownSound();
				program->terminate = true;
				//program = programs[0];
				nextProgram = "Main menu";

				break;
			}
		}
	}

	void updateScreen() {

		screen.printf("Seed %d\r\n", seed);
		screen.printf("%s %s %d\r\n", arrangementScript.c_str(), structureScript.c_str(), tempo);
//		screen.printf("%s\r\n", structureScript.c_str());
//		screen.printf("Tempo %d\r\n", tempo);
		if (currentSel == 0)
			screen.printf(">");
		else
			screen.printf(" ");

		if (isPlaying) {
			screen.printf("Pause ");
		}
		else {
			screen.printf("Play ");
		}

		if (currentSel == 1)
			screen.printf(">");
		else
			screen.printf(" ");

		screen.printf("Next ");

		if (currentSel == 2)
			screen.printf(">");
		else
			screen.printf(" ");

		screen.printf("Quit\r\n");


	}

	string name() {
		return "Music generator";
	}

private:

	void nextSeed()
	{
		midiWriter.setStop(true);
	}

	void setRandomSeed() {
		seed = rng.randint(0, 2147483647);
		srand(seed);
		arrangementScript = arrangements[Utils::getRandomInt(0, ARRANGEMENTS_COUNT-1)];
//	arrangementScript="Random Electro Rock";
		structureScript = structures[Utils::getRandomInt(0, STRUCTURES_COUNT-1)];

		tempo = tempos[Utils::getRandomInt(0, TEMPO_COUNT-1)];

/*	arrangementScript="Simple Dance Style Arrangement";
	structureScript="Modern Song Structure";
	tempo=120;
	seed=134;
	*/
	}

};

// MIDI File player program

void smfMidiCallback(midi_event *pev)
{
	if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0))
	{

		MidiDriver::QueueMessage qm;
		uint8_t *m = qm.getMessage();

		m[0] = pev->data[0] | pev->channel;
		for (uint8_t i = 1; i < pev->size; i++)
		{
			m[i] = pev->data[i];
		}

		qm.setSize(pev->size);
		qm.setTime(pev->timestamp);
		midiWriter.queueMessage(qm);
	}
	else {

		MidiDriver::QueueMessage qm;
		uint8_t *m = qm.getMessage();

		for (uint8_t i = 0; i < pev->size; i++)
		{
			m[i] = pev->data[i];
		}

		qm.setSize(pev->size);
		qm.setTime(pev->timestamp);
		midiWriter.queueMessage(qm);

	}
}

void smfSysexCallback(sysex_event *pev)
{
//	MidiDriver::QueueMessage qm;
//	uint8_t *m = qm.getMessage();

	for (uint8_t i = 0; i < pev->size; i++)
	{
//		m[i] = pev->data[i];
		midiOut.send(pev->data[i]);
	}
/*
	qm.setSize(pev->size);
	qm.setTime(0);
	midiWriter.queueMessage(qm);
	*/
}

class MidiFileProgram : public Program {
	MD_MIDIFile SMF;
	int file_err, dir_err;
	string midiFilename;

	int currentSel;

public:
	MidiFileProgram() : Program() {
		currentSel = 0;
		initMidiPlayer();
	}

	void process() {
		activateSound();

		currentSel = 0;

		play();

		while (!terminate) {
			wait_ms(100);
			processMidiFile(NULL);
		}
	}

	void parsePad(int pad_gpio) {

		if (pad_gpio == PAD_PREVIOUS) {
			if (currentSel > 0)
				currentSel--;
		}

		if (pad_gpio == PAD_NEXT) {
			if (currentSel < 2)
				currentSel++;
		}

		// select
		if (pad_gpio == PAD_SELECT) {
			switch (currentSel) {
			case 0:
				if (isPlaying) {
					pause();
				}
				else {
					play();
				}
				break;
			case 1:
				stop();
				break;
			case 2:
				stop();
				shutdownSound();
				program->terminate = true;
				//program = programs[0];
				nextProgram = "Main menu";
				break;
			}
		}

	}
	void updateScreen() {

		if (dir_err < 0 && file_err < 0) {
			screen.printf("Playing MIDI file %s\r\n", midiFilename.c_str());
		} else {
			if (dir_err == 0) {
				screen.printf("Could not open SD Card\r\n");
			} else {
				if (file_err == 0) {
					screen.printf("Could not open %s\r\n", midiFilename.c_str());
				} else {
					screen.printf("Loading\r\n");
				}
			}
		}

		if (currentSel == 0)
			screen.printf(">");
		else
			screen.printf(" ");

		if (isPlaying) {
			screen.printf("Pause ");
		}
		else {
			screen.printf("Play ");
		}

		if (currentSel == 1)
			screen.printf(">");
		else
			screen.printf(" ");

		screen.printf("Next ");

		if (currentSel == 2)
			screen.printf(">");
		else
			screen.printf(" ");

		screen.printf("Quit\r\n");

	}
	string name() {
		return "MIDI file player";
	}

private:

	void initMidiPlayer() {
		sd.large_frames(true);

		SMF.begin("sd");
		SMF.setMidiHandler(smfMidiCallback);
		SMF.setSysexHandler(smfSysexCallback);
	}

	void testSD() {
		FILE *fp = fopen("/sd/sdtest2.txt", "w");
		if (fp == NULL) {
			error("Could not open file for write\n");
		}
		fprintf(fp, "Hello fun SD Card World!");
		fclose(fp);
	}


	void processMidiFile(void const *args) {
		dir_err = -1;
		file_err = -1;

		DIR *d;                 // Directory pointer
		dirent *file;

		if ((d = opendir("/sd")) == NULL) { // Attempt to open directory at given location
			dir_err = 0;
		} else {
			// If Successful
			while ((file = readdir(d)) != NULL and !terminate) {

				play();
				string fn(file->d_name);
				midiFilename = fn;

				SMF.setFilename(("/sd/" + fn).c_str()); //"/sd/test.mid");
				file_err = SMF.load();

				midiWriter.setTicksPerQuarterNote(SMF.getTicksPerQuarterNote());
				midiWriter.sendTempo(0, 0, 60000000 / SMF.getTempo());

				if (file_err < 0) {
					while (!SMF.isEOF())
					{
						screenMutex.lock();


						midiWriter.wait();
						if (midiWriter.getQueueSize() <= 100)
							SMF.getNextEvent();

						screenMutex.unlock();

						if (midiWriter.getStop()) {
							break;
						}
					}
					// done with this one
					SMF.close();
				}
				wait_ms(100);
				//osSignalWait(0x1, osWaitForever);
				while (midiWriter.getQueueSize() > 0) {
					wait_ms(100);
				}


			}
			closedir(d);
		}
	}

};


class EmptyProgram : public Program {
public:
	EmptyProgram() : Program() {
	}

	void process() {
		while (!terminate)
		{
			wait_ms(100);
		}

	}

	void parsePad(int pad_gpio) {
	}

	void updateScreen() {
	}

	string name() {
		return "Empty";
	}

};

class SynthesizerProgram : public Program {
public:
	SynthesizerProgram() : Program() {
	}

	void process() {
		activateSound();

		while (!terminate)
		{
			wait_ms(100);
		}
	}

	void parsePad(int pad_gpio) {
		if (pad_gpio == PAD_SELECT) {

			stop();
			shutdownSound();
			program->terminate = true;
//			program = programs[0];
			nextProgram = "Main menu";

		}

	}

	void updateScreen() {
		screen.printf(">");
		screen.printf("Quit\r\n");
	}

	string name() {
		return "Synthesizer";
	}

};

// https://github.com/rbirkby/ArduinoTetris
#define TETRIS_WIDTH 10
#define TETRIS_HEIGHT 14
#define TETRIS_BLOCK 4

class TetrisProgram : public Program {

	const int lead_notes[58] = {
		// part 1
		76, 71, 72, 74, 72, 71, 69, 69, 72, 76, 74, 72, 71, 71, 72, 74, 76, 72, 69, 69, 0,
		74, 77, 81, 79, 77, 76, 72, 76, 74, 72, 71, 71, 72, 74, 76, 72, 69, 69, 0,

		// part 2
		64, 60, 62, 59, 60, 57, 56, 59,
		64, 60, 62, 59, 60, 64, 69, 69, 68, 0

	};
	const float lead_times[58] = {
		// part 1
		1.0, 0.5, 0.5, 1.0, 0.5, 0.5, 1.0, 0.5, 0.5, 1.0, 0.5, 0.5, 1.0, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
		1.5, 0.5, 1.0, 0.5, 0.5, 1.5, 0.5, 1.0, 0.5, 0.5, 1.0, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,

		// part 2
		2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0,
		2.0, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0, 1.0, 3.0, 1.0

	};

	const int bass_notes[128] = {
		// part 1
		40, 52, 40, 52, 40, 52, 40, 52, 33, 45, 33, 45, 33, 45, 33, 45, 32, 44, 32, 44, 32, 44, 32, 44, 33, 45, 33, 45, 33, 47, 48, 52,
		38, 50, 38, 50, 38, 50, 38, 50, 36, 48, 36, 48, 36, 48, 36, 48, 35, 47, 35, 47, 35, 47, 35, 47, 33, 45, 33, 45, 33, 45, 33, 45,

		// part 2
		33, 40, 33, 40, 33, 40, 33, 40, 32, 40, 32, 40, 32, 40, 32, 40, 33, 40, 33, 40, 33, 40, 33, 40, 32, 40, 32, 40, 32, 40, 32, 40,
		33, 40, 33, 40, 33, 40, 33, 40, 32, 40, 32, 40, 32, 40, 32, 40, 33, 40, 33, 40, 33, 40, 33, 40, 32, 40, 32, 40, 32, 40, 32, 40


	};
	const float bass_times[128] = {
		// part 1
		0.5,  0.5,  0.5,  0.5,  0.5,  0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5,
		0.5,  0.5,  0.5,  0.5,  0.5,  0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5,

		// part 2
		0.5,  0.5,  0.5,  0.5,  0.5,  0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5,
		0.5,  0.5,  0.5,  0.5,  0.5,  0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5

	};

	int curLeadNote;
	int curBassNote;
	long int curLeadTime;
	long int curBassTime;

	struct Piece {
		unsigned int *type;
		int x;
		int y;
		int dir;
	};

	struct Block {
		int x;
		int y;
	};

	enum DIR {
		UP,
		RIGHT,
		DOWN,
		LEFT,
		MIN = UP,
		MAX = LEFT
	};

	int blocks[TETRIS_WIDTH][TETRIS_HEIGHT];                               // 2 dimensional array (nx*ny) representing tetris court - either empty block or occupied by a 'piece'
	bool playing = true;                              // true|false - game is in progress
	bool lost = false;
	Piece current, next;                              // the current and next piece
	unsigned int score = 0;                           // the current score
	int dir = 0;

	unsigned int i[4] = {0x0F00, 0x2222, 0x00F0, 0x4444};
	unsigned int j[4] = {0x44C0, 0x8E00, 0x6440, 0x0E20};
	unsigned int l[4] = {0x4460, 0x0E80, 0xC440, 0x2E00};
	unsigned int o[4] = {0xCC00, 0xCC00, 0xCC00, 0xCC00};
	unsigned int s[4] = {0x06C0, 0x8C40, 0x6C00, 0x4620};
	unsigned int t[4] = {0x0E40, 0x4C40, 0x4E00, 0x4640};
	unsigned int z[4] = {0x0C60, 0x4C80, 0xC600, 0x2640};

	unsigned int *pieces[7] = {i, j, l, o, s, t, z};

public:
	TetrisProgram() : Program() {
		int seed = rng.randint(0, 2147483647);
		srand(seed);

		reset();
		curLeadNote = 0;
		curBassNote = 0;
		curLeadTime = 0;
		curBassTime = 0;

	}

	void process() {
		activateSound();

		play();
		curLeadNote = 0;
		curBassNote = 0;
		curLeadTime = 0;
		curBassTime = 0;

		while (!terminate)
		{
			playMusic();
			drop();
			wait_ms(500);

		}

	}

	void parsePad(int pad_gpio) {
		if (pad_gpio == PAD_TOP) {
			rotate();
		}

		if (pad_gpio == PAD_BOTTOM) {
		}

		if (pad_gpio == PAD_PREVIOUS) {
			move(LEFT);
		}

		if (pad_gpio == PAD_NEXT) {
			move(RIGHT);
		}
		if (pad_gpio == PAD_SELECT) {
			stop();
			shutdownSound();
			program->terminate = true;
//			program = programs[0];
			nextProgram = "Main menu";

		}
	}

	void updateScreen() {
		draw();
	}

	string name() {
		return "Tetris";
	}
private:

	void playMusic() {
		while (midiWriter.getQueueSize() < 100) {

			int lead_note = lead_notes[curLeadNote];
			float lead_note_time_remaining = lead_times[curLeadNote];
			if (lead_note > 0) {
				midiWriter.sendNoteOn(curLeadTime, 0, 1, lead_note, 127);
				curLeadTime += 60 * lead_note_time_remaining;
				midiWriter.sendNoteOff(curLeadTime, 0, 1, lead_note, 127);
			} else {
				curLeadTime += 60 * lead_note_time_remaining;

			}

			int bass_note = bass_notes[curBassNote];
			float bass_note_time_remaining = bass_times[curBassNote];
			if (bass_note > 0) {
				midiWriter.sendNoteOn(curBassTime, 0, 2, bass_note, 127);
				curBassTime += 60 * bass_note_time_remaining;
				midiWriter.sendNoteOff(curBassTime, 0, 2, bass_note, 127);
			} else {
				curBassTime += 60 * bass_note_time_remaining;
			}

			curLeadNote++;
			curBassNote++;

			if (curLeadNote >= 58 || curBassNote >= 128) {
				curLeadNote = 0;
				curBassNote = 0;

				curBassTime = curLeadTime;
			}
		}
	}

	void lose() { playing = false; lost = true; }

	void addScore(int n) { score = score + n; }
	void clearScore() { score = 0; }
	int getBlock(int x, int y) { return (blocks[x] ? blocks[x][y] : NULL); }
	void setBlock(int x, int y, int type) { blocks[x][y] = type; }
	void clearBlocks() { for (int x = 0; x < TETRIS_WIDTH; x++) { for (int y = 0; y < TETRIS_HEIGHT; y++) { blocks[x][y] = NULL; } } }
	void setCurrentPiece(Piece piece) {current = piece;}
	void setNextPiece(Piece piece) {next = piece;}

	void reset() {
		lost = false;
		playing = true;
		clearBlocks();
		clearScore();
		setCurrentPiece(randomPiece());
		setNextPiece(randomPiece());
	}

	bool move(int dir) {
		int x = current.x, y = current.y;
		switch (dir) {
		case RIGHT: x = x + 1; break;
		case LEFT:  x = x - 1; break;
		case DOWN:  y = y + 1; break;
		}
		if (unoccupied(current.type, x, y, current.dir)) {
			current.x = x;
			current.y = y;
			return true;
		}
		else {
			return false;
		}
	}

	void rotate() {
		int newdir = (current.dir == MAX ? MIN : current.dir + 1);
		if (unoccupied(current.type, current.x, current.y, newdir)) {
			current.dir = newdir;
		}
	}

	void drop() {
		if (!move(DOWN)) {
			addScore(10);
			dropPiece();
			removeLines();
			setCurrentPiece(next);
			setNextPiece(randomPiece());

			if (occupied(current.type, current.x, current.y, current.dir)) {
				lose();
			}
		}
	}

	void dropPiece() {
		Block positionedBlocks[4];
		getPositionedBlocks(current.type, current.x, current.y, current.dir, positionedBlocks);

		for (int i = 0; i < 4; i++) {
			setBlock(positionedBlocks[i].x, positionedBlocks[i].y, -1);
		}
	}

	void removeLines() {
		int x, y, n = 0;
		bool complete;

		for (y = TETRIS_HEIGHT; y > 0; --y) {
			complete = true;

			for (x = 0; x < TETRIS_WIDTH; ++x) {
				if (!getBlock(x, y)) complete = false;
			}

			if (complete) {
				removeLine(y);
				y = y + 1; // recheck same line
				n++;
			}
		}
		if (n > 0) {
			addScore(100 * pow(2, n - 1)); // 1: 100, 2: 200, 3: 400, 4: 800
		}
	}

	void removeLine(int n) {
		int x, y;
		for (y = n; y >= 0; --y) {
			for (x = 0; x < TETRIS_WIDTH; ++x) {
				setBlock(x, y, (y == 0) ? NULL : getBlock(x, y - 1));
			}
		}
	}

//-------------------------------------------------------------------------
// RENDERING
//-------------------------------------------------------------------------

	void draw() {
		screen.drawRect(6 * 4 - 2, 4 - 2, TETRIS_WIDTH * 4 + 4, TETRIS_HEIGHT * 4 + 4, 1);

		drawCourt();
		drawNext();
		drawScore();

		removeLines();
// uView.display();
	}

	void drawCourt() {
// uView.clear(PAGE);
		if (playing)
			drawPiece(current.type, current.x + 6, current.y, current.dir);

		int x, y;
		for (y = 0 ; y < TETRIS_HEIGHT ; y++) {
			for (x = 0 ; x < TETRIS_WIDTH ; x++) {
				if (getBlock(x, y))
					drawBlock(x + 6, y);
			}
		}

		if (lost) {
			screen.setTextCursor(0, 2);
			screen.printf("Game over\r\n");
		}
	}

	void drawNext() {
		if (playing)
			drawPiece(next.type, 0, 3, next.dir);
	}

	void drawScore() {
		screen.printf("%d\r\n", score);
	}

	void drawPiece(unsigned int type[], int x, int y, int dir) {
		Block positionedBlocks[4];
		getPositionedBlocks(type, x, y, dir, positionedBlocks);

		for (int i = 0; i < 4; i++) {
			drawBlock(positionedBlocks[i].x, positionedBlocks[i].y);
		}
	}

	void drawBlock(int x, int y) {
		screen.drawRect(x * TETRIS_BLOCK, y * TETRIS_BLOCK + 4, TETRIS_BLOCK, TETRIS_BLOCK, 1);
	}


//------------------------------------------------
// do the bit manipulation and iterate through each
// occupied block (x,y) for a given piece
//------------------------------------------------
	void getPositionedBlocks(unsigned int type[], int x, int y, int dir, Block* positionedBlocks) {
		unsigned int bit;
		int blockIndex = 0, row = 0, col = 0, blocks = type[dir];

		for (bit = 0x8000; bit > 0 ; bit = bit >> 1) {
			if (blocks & bit) {
				Block block = {x + col, y + row};

				positionedBlocks[blockIndex++] = block;
			}
			if (++col == 4) {
				col = 0;
				++row;
			}
		}
	}

//-----------------------------------------------------
// check if a piece can fit into a position in the grid
//-----------------------------------------------------
	bool occupied(unsigned int type[], int x, int y, int dir) {
		bool result = false;

		Block positionedBlocks[4];
		getPositionedBlocks(type, x, y, dir, positionedBlocks);

		for (int i = 0; i < 4; i++) {
			Block block = positionedBlocks[i];

			if ((block.x < 0) || (block.x >= TETRIS_WIDTH) || (block.y < 0) || (block.y >= TETRIS_HEIGHT) || getBlock(block.x, block.y))
				result = true;
		}

		return result;
	}

	bool unoccupied(unsigned int type[], int x, int y, int dir) {
		return !occupied(type, x, y, dir);
	}


	Piece randomPiece() {
		Piece current = {pieces[rand() % 6], 3, 0, UP};
		return current;
	}

};

// main menu program

class MainMenuProgram : public Program {
	int currentProgram;
public:
	MainMenuProgram() : Program() {
		currentProgram = 1;
	}

	void process() {
		while (!terminate)
		{
			wait_ms(100);
		}
	}

	void parsePad(int pad_gpio) {
		if (pad_gpio == PAD_TOP)
		{
			if (currentProgram > 1)
				currentProgram--;
		}

		if (pad_gpio == PAD_BOTTOM)
		{
			if (currentProgram < programs.size() - 1)
				currentProgram++;
		}

		if (pad_gpio == PAD_SELECT)
		{
			program->terminate = true;
			nextProgram =  programs[currentProgram];
//			program = programs[currentProgram];
		}

	}

	void updateScreen() {
		for (int i = 1; i < programs.size(); i++) {
			if (i == currentProgram) {
				screen.printf(">");
			} else {
				screen.printf(" ");
			}
			screen.printf("%s\r\n", programs[i].c_str());
		}
	}

	string name() {
		return "Main menu";
	}

};

void initPrograms() {
	nextProgram = "Main menu";
	/*
		programs[0] = new MainMenuProgram();
		programs[1] = new CcgProgram();
		programs[2] = new MidiFileProgram();
		programs[3] = new SynthesizerProgram();
		programs[4] = new TetrisProgram();
	*/
	programsMap["Main menu"] = makeProgram<MainMenuProgram>;
	programsMap["Music generator"] = makeProgram<CcgProgram>;
	programsMap["MIDI file player"] = makeProgram<MidiFileProgram>;
	programsMap["Synthesizer"] = makeProgram<SynthesizerProgram>;
	programsMap["Tetris"] = makeProgram<TetrisProgram>;

	programs.push_back("Main menu");
	programs.push_back("Music generator");
	programs.push_back("MIDI file player");
	programs.push_back("Synthesizer");
	programs.push_back("Tetris");

	/*
	    for (map<string, ProgramCreator>::iterator it = programsMap.begin(); it != programsMap.end(); ++it)
	        programs.push_back(it->first);
	*/
};

int main() {
//	mainThreadID = osThreadGetId();
//	mainThreadID = Thread::gettid();
	initPrograms();
//	program = programs[0];
	program = programsMap["Main menu"]();
//	program = new MidiFileProgram();

	wait_ms(100);
	initJackSense();
	wait_ms(100);
	initPad();

	wait_ms(100);

	screen.setRotation(2);
	screen.fillScreen(BLACK);
	screen.display();

	setClockOutput();

//	midiOut.setVolume(127);

	wait_ms(100);

	midiUsb.attach(transmitMessage);


	Thread midiThread(processMidiDriver, NULL, osPriorityRealtime);

	Thread interfaceThread(processInterface, NULL, osPriorityNormal);

	wait_ms(1000);

	while (1) {
		screenMutex.lock();
		delete program;
		program = programsMap[nextProgram]();
		screenMutex.unlock();
		program->terminate = false;
		program->process();
		wait_ms(500);
	}

}
