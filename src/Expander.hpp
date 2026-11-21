#pragma once
#include <stdint.h>

// What XL-185 tells an XL-185 MOD directly on its right, once per sample. The MOD owns both buffers
// (leftExpander.producerMessage / consumerMessage); XL-185 writes the producer and requests the flip,
// so the MOD reads it one sample later.
// A MOD passes each message it reads on to a MOD on its own right in the same way, so MODs chain;
// every link adds one sample.

static const uint32_t EXPANDER_MARKER = 0x58313835u;	// "X185": a sequencer wrote this message

struct ExpanderMessage {
	uint32_t marker;	// EXPANDER_MARKER, 0 in a buffer nothing has written
	uint32_t frame;	// counts up every sample, so a repeated read is not a new event
	int stage;	// playing stage 1..8, 0 none
	bool stageStart;	// a stage started on this sample
	bool stepRan;	// a step ran on this sample
	bool reset;	// a RESET edge on this sample
	bool run;
	int length;	// stages in use, 1..8
	uint8_t lockBits;	// bit i: stage i + 1 locked
	uint8_t onBits;	// bit i: stage i + 1 on
	int stepsPerBar;
	int theme;

	ExpanderMessage() : marker(0), frame(0), stage(0), stageStart(false), stepRan(false), reset(false), run(false),
		length(8), lockBits(0), onBits(0xFF), stepsPerBar(16), theme(0) {}
};
