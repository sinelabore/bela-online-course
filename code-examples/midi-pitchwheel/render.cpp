/*
 ____  _____ _        _    
| __ )| ____| |      / \   
|  _ \|  _| | |     / _ \  
| |_) | |___| |___ / ___ \ 
|____/|_____|_____/_/   \_\

http://bela.io

C++ Real-Time Audio Programming with Bela - Lecture 16: MIDI part 2
midi-pitchwheel: MIDI synth with support for pitch wheel and other controls
*/


#include <Bela.h>
#include <libraries/Gui/Gui.h>
#include <libraries/GuiController/GuiController.h>
#include <libraries/Scope/Scope.h>
#include <libraries/Midi/Midi.h>
#include <cmath>

#include "ADSR.h"
#include "Wavetable.h"
#include "Filter.h"
#include "ExponentialSegment.h"

// Device for handling MIDI messages
Midi gMidi;

// Name of the MIDI port to use. Run 'amidi -l' on the console to see a list.
// Typical values: 
//   "hw:0,0,0" for a virtual device (from the computer)
//   "hw:1,0,0" for a USB device plugged into the Bela board
const char* gMidiPort0 = "hw:1,0,0";

// Oscillator and Filter objects
Wavetable gOscillator;
Filter gFilter;

// ADSR objects
ADSR gAmplitudeADSR, gFilterADSR;
float gAmplitude = 0.0;

// Pitch state information
float gCentreFrequency = 440.0;			// Frequency of the note without pitch bend

// Handling for multiple MIDI notes
const int kMaxActiveNotes = 16;
int gActiveNotes[kMaxActiveNotes];
int gActiveNoteCount = 0;

// Browser-based GUI to adjust parameters
Gui gGui;
GuiController gGuiController;

// Bela Oscilloscope
Scope gScope;

// MIDI callback function
void midiEvent(MidiChannelMessage message, void *arg);

bool setup(BelaContext *context, void *userData)
{
	// Initialise the MIDI device
	if(gMidi.readFrom(gMidiPort0) < 0) {
		rt_printf("Unable to read from MIDI port %s\n", gMidiPort0);
		return false;
	}
	gMidi.writeTo(gMidiPort0);
	gMidi.enableParser(true);
	gMidi.setParserCallback(midiEvent, (void *)gMidiPort0);
	
	std::vector<float> wavetable;
	const unsigned int wavetableSize = 512;
		
	// Populate a buffer with the first 64 harmonics of a sawtooth wave
	wavetable.resize(wavetableSize);
	for(unsigned int n = 0; n < wavetable.size(); n++) {
		wavetable[n] = 0;
		for(unsigned int harmonic = 1; harmonic <= 48; harmonic++) {
			wavetable[n] += 0.5 * sinf(2.0 * M_PI * (float)harmonic * (float)n / 
								 (float)wavetable.size()) / (float)harmonic;
		}
	}
	
	// Initialise the wavetable, passing the sample rate and the buffer
	gOscillator.setup(context->audioSampleRate, wavetable);

	// Initialise the filter
	gFilter.setSampleRate(context->audioSampleRate);

	// Initialise the ADSR objects
	gAmplitudeADSR.setSampleRate(context->audioSampleRate);
	gFilterADSR.setSampleRate(context->audioSampleRate);
	
	// Set up the GUI
	gGui.setup(context->projectName);
	gGuiController.setup(&gGui, "ADSR Controller");	
	
	// Arguments: name, minimum, maximum, increment, default value
	gGuiController.addSlider("Amplitude Attack time", 0.01, 0.001, 0.1, 0);
	gGuiController.addSlider("Amplitude Decay time", 0.05, 0.01, 0.3, 0);
	gGuiController.addSlider("Amplitude Sustain level", 0.3, 0, 1, 0);
	gGuiController.addSlider("Amplitude Release time", 0.2, 0.001, 2, 0);
	
	gGuiController.addSlider("Filter base frequency", 200, 50, 1000, 0);
	gGuiController.addSlider("Filter sensitivity", 3000, 0, 10000, 0);
	gGuiController.addSlider("Filter Q", 4, 0.5, 10, 0);
	gGuiController.addSlider("Filter Attack time", 0.05, 0.001, 0.1, 0);
	gGuiController.addSlider("Filter Decay time", 0.1, 0.01, 0.3, 0);
	gGuiController.addSlider("Filter Sustain level", 0.6, 0, 1, 0);
	gGuiController.addSlider("Filter Release time", 0.3, 0.001, 2, 0);

	// Initialise the scope
	gScope.setup(3, context->audioSampleRate);
	
	return true;
}

// Calculate the frequency based on note and pitch bend
float calculateFrequency(int noteNumber)
{
	return powf(2.0, (noteNumber - 69)/12.0) * 440.0;
}

// MIDI note on received
void noteOn(int noteNumber, int velocity) 
{
	// Check if we have any note slots left
	if(gActiveNoteCount < kMaxActiveNotes) {
		// Keep track of this note, then play it
		gActiveNotes[gActiveNoteCount++] = noteNumber;
		
		// Map note number to frequency
		gCentreFrequency = calculateFrequency(noteNumber);
		
		// Map velocity to amplitude on a decibel scale
		float decibels = map(velocity, 1, 127, -40, 0);
		gAmplitude = powf(10.0, decibels / 20.0);
	
		// Start the ADSR if this was the first note pressed
		if(gActiveNoteCount == 1) {
			gAmplitudeADSR.trigger();
			gFilterADSR.trigger();
		}
	}
}

// MIDI note off received
void noteOff(int noteNumber)
{
	bool activeNoteChanged = false;
	
	// Go through all the active notes and remove any with this number
	for(int i = gActiveNoteCount - 1; i >= 0; i--) {
		if(gActiveNotes[i] == noteNumber) {
			// Found a match: is it the most recent note?
			if(i == gActiveNoteCount - 1) {
				activeNoteChanged = true;
			}
	
			// Move all the later notes back in the list
			for(int j = i; j < gActiveNoteCount - 1; j++) {
				gActiveNotes[j] = gActiveNotes[j + 1];
			}
			gActiveNoteCount--;
		}
	}
	
	if(gActiveNoteCount == 0) {
		// No notes left: stop the ADSR
		gAmplitudeADSR.release();	
		gFilterADSR.release();
	}
	else if(activeNoteChanged) {
		// Update the frequency but don't retrigger
		int mostRecentNote = gActiveNotes[gActiveNoteCount - 1];
		
		gCentreFrequency = calculateFrequency(mostRecentNote);
	}
}

void render(BelaContext *context, void *userData)
{
	// Retrieve values from the sliders
	float ampAttackTime = gGuiController.getSliderValue(0);
	float ampDecayTime = gGuiController.getSliderValue(1);
	float ampSustainLevel = gGuiController.getSliderValue(2);
	float ampReleaseTime = gGuiController.getSliderValue(3);
	float filterBase = gGuiController.getSliderValue(4);
	float filterSensitivity = gGuiController.getSliderValue(5);
	float filterQ = gGuiController.getSliderValue(6);
	float filterAttackTime = gGuiController.getSliderValue(7);
	float filterDecayTime = gGuiController.getSliderValue(8);
	float filterSustainLevel = gGuiController.getSliderValue(9);
	float filterReleaseTime = gGuiController.getSliderValue(10);
	
	// Set oscillator, ADSR and filter parameters
	gAmplitudeADSR.setAttackTime(ampAttackTime);
	gAmplitudeADSR.setDecayTime(ampDecayTime);
	gAmplitudeADSR.setSustainLevel(ampSustainLevel);
	gAmplitudeADSR.setReleaseTime(ampReleaseTime);
	gFilterADSR.setAttackTime(filterAttackTime);
	gFilterADSR.setDecayTime(filterDecayTime);
	gFilterADSR.setSustainLevel(filterSustainLevel);
	gFilterADSR.setReleaseTime(filterReleaseTime);
	gFilter.setQ(filterQ);	

	// Now calculate the audio for this block
	for(unsigned int n = 0; n < context->audioFrames; n++) {
		// Calculate the oscillator frequency
		// TODO: include the effect of pitch wheel (you'll need a global variable)
		float frequency = gCentreFrequency;
		gOscillator.setFrequency(frequency);
		
		// Get the next value from the ADSR envelope, scaled by the global amplitude
    	float amplitude = gAmplitude * gAmplitudeADSR.process();
    	
		// Set the filter frequency 
		float filterControl = gFilterADSR.process();
		gFilter.setFrequency(filterBase + filterSensitivity * filterControl);
    	
    	// Calculate the output
    	float out = gOscillator.process() * amplitude;
    	out = 0.5 * gFilter.process(out);
    	
    	for(unsigned int channel = 0; channel < context->audioOutChannels; channel++) {
			// Write the sample to every audio output channel
    		audioWrite(context, n, channel, out);
    	}
    	
    	// Log the audio output and the envelope to the scope
    	gScope.log(out, amplitude, filterControl);
	}
}

// This callback function is called every time a new MIDI message is available
// This happens on a different thread than the audio processing

void midiEvent(MidiChannelMessage message, void *arg) {
	// Display the port, if available
	if(arg != NULL) {
		rt_printf("Message from midi port %s ", (const char*) arg);
	}
	
	// Display the message
	message.prettyPrint();
		
	// A MIDI "note on" message type might actually hold a real
	// note onset (e.g. key press), or it might hold a note off (key release).
	// The latter is signified by a velocity of 0.
	if(message.getType() == kmmNoteOn) {
		int noteNumber = message.getDataByte(0);
		int velocity = message.getDataByte(1);
		
		// Velocity of 0 is really a note off
		if(velocity == 0) {
			noteOff(noteNumber);
		}
		else {
			noteOn(noteNumber, velocity);
		}
	}
	else if(message.getType() == kmmNoteOff) {
		// We can also encounter the "note off" message type which is the same
		// as "note on" with a velocity of 0.
		int noteNumber = message.getDataByte(0);
		
		noteOff(noteNumber);
	}
	// TODO: handle pitchwheel messages (type kmmPitchBend)
}

void cleanup(BelaContext *context, void *userData)
{
	
}

