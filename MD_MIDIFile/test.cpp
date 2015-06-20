#include "RtMidi.h"
#include "MD_MIDIFile.h"
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <vector>

using namespace std;

RtMidiOut *midiout;
MD_MIDIFile SMF;


void smfMidiCallback(midi_event *pev)
{
  if ((pev->data[0] >= 0x80) && (pev->data[0] <= 0xe0))
  {

//    printf("pev->timestamp %ld\n",pev->timestamp);
    vector<unsigned char> tmsg;

    tmsg.push_back((unsigned char)pev->data[0] | pev->channel);

    for (uint8_t i = 1; i < pev->size; i++) {
      tmsg.push_back((unsigned char)pev->data[i]);

    }
    midiout->sendMessage( &tmsg );
  }
  else {
    vector<unsigned char> tmsg;

    for (uint8_t i = 0; i < pev->size; i++) {
      tmsg.push_back((unsigned char)pev->data[i]);

    }
    midiout->sendMessage( &tmsg );
  }
}

void smfSysexCallback(sysex_event *pev)
{
  vector<unsigned char> tmsg;

  for (uint8_t i = 0; i < pev->size; i++) {
    tmsg.push_back((unsigned char)pev->data[i]);

  }
  midiout->sendMessage( &tmsg );
}

int main(int argc, char** argv) {
  int port = 0;
  midiout = new RtMidiOut();
  midiout->openPort( port );

  SMF.begin("sd");

  SMF.setMidiHandler(smfMidiCallback);
  SMF.setSysexHandler(smfSysexCallback);

  SMF.setFilename("./test.mid");
  int err = SMF.load();
  printf("ticktime %d %d %d\n",SMF.getTicksPerQuarterNote(),SMF.getTickTime(),SMF.getTempo());
  while (!SMF.isEOF())
  {
    SMF.getNextEvent();
  }

  // done with this one
  SMF.close();

  return 0;
}