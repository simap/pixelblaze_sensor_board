
#include "main.h"

char outBuffer[512];
int outBufferLen;

uint8_t nib2hex(uint8_t in);
void jsonKey(char ** pp, const char * key, uint32_t value, int places);
void jsonFraction(char ** pp, uint32_t value, int places);

//const char header[5] = "SB10"; //sensor board 1.0 packet header

#define WRITEOUT(v) {memcpy(out, &v, sizeof(v)); out+= sizeof(v);}

void processSensorData(int16_t * audioBuffer, volatile uint16_t adcBuffer[7], volatile int16_t accelerometer[3]) {
	union {
		int16_t imaginary[N]; //imaginary component
		uint16_t magnitude[N]; //calculated magnitude
	} tmp;

	//start making output buffer
	char * out = outBuffer;
	WRITEOUT("SB10");

	//init imaginary with zeros and calculate average (DC offset)
	uint32_t total = 0;
	for (int i = 0; i < N; i++) {
		tmp.imaginary[i] = 0;
		total += audioBuffer[i];
	}
	int average = total >> NLOG2;
	uint32_t energyTotal = 0;
	for (int i = 0; i < N; i++) {
		//filter out DC (subtract the average) then
		//apply a windowing function, borrowing Sinewave LUT from fix_fft
		//the positive bit of Sinewave ranges from index 0-512 (9 bit positions)
		//could probably optimize: si = i * 512 / N == (i << 9) >> 9 == i
		int si = i * 512 / N;
		audioBuffer[i] -= average;
		energyTotal += abs(audioBuffer[i]);
		audioBuffer[i] = (Sinewave[si] * audioBuffer[i]) >> 16;
	}
	uint16_t energyAverage = energyTotal >> NLOG2;

	//run the FFT (runs in place, overwriting buf and imaginary)
	fix_fft(audioBuffer, &tmp.imaginary[0], NLOG2, 0);

	//an fft of 512 gives us 256 buckets of frequency info. at 20khz, each has ~38Hz
	//we don't really want all 256 buckets of frequency info
	//if we compressed this linearly, we'd lose a lot of low/mid tone info

	//compress these on an exponential curve.
	//each pass gives new top frequency that is at least 1 bucket higher than the last pass
	//run through and average all the buckets up to that new top
	int k = 0;
	int i = 0;
	uint32_t acc = 0; // 16.16 fixed point for the curve
	// acc = acc * 1.1172 + 1
	for (; acc <= N/2 << 16; k++) {
		acc = ((uint64_t)acc * (uint64_t)CURVE_EXP) >> 16;
		acc += 0x10000;
		//calculate magnitude of frequency buckets for this group
		int top = acc >> 16;
		int size = top - i;
		uint32_t total = 0;
		for (; i < top; i++) {
			//using the fix16_sqrt gives us a bit more resolution as we get
			//8 bits more using this over an integer sqrt
			int32_t t = fix16_sqrt(
					tmp.imaginary[i] * tmp.imaginary[i] +
					audioBuffer[i] * audioBuffer[i]);

			//we can't keep all those extra bits, but 4 of 8 seems like a good value
			//as this only overloads a little and only for REALLY LOUD inputs
			t >>= 4;
			total += t;
		}
		uint32_t average = total / size;
		//cap to 16 bit value
		if (average > 0xffff)
			average = 0xffff;
		uint16_t average16 = (uint16_t) average;
		//write this out
		WRITEOUT(average16);
	}

	WRITEOUT(energyAverage);


	for (int i = 1; i < 7; i++) {
		uint16_t v = adcBuffer[i]<<4;
		WRITEOUT(v);
	}

	for (int i = 0; i < 3; i++) {
		uint16_t v = accelerometer[i];
		WRITEOUT(v);
	}



	*(out - 1) = '\n'; //replace last comma
	//NOTE: no null terminator necessary.

	outBufferLen = out - outBuffer;
	writeToUsart((uint8_t *) outBuffer, outBufferLen);
}

uint8_t nib2hex(uint8_t in) {
	in &= 0xf;
	if (in < 10)
		return '0' + in;
	else
		return 'a' + (in-10);
}

void jsonKey(char ** pp, const char * key, uint32_t value, int places) {
	char * p = *pp;
	*p++ = '"';
	//copy key until null
	while ((*p++ = *key++) != 0)
		;
	*(p-1) = '"'; //replace the copied null
	*p++ = ':';
	*pp = p;
}

void jsonFraction(char ** pp, uint32_t value, int places) {
	char * p = *pp;
	*p++ = '.';
	//only write the lower 16 bits of value as a fractional up to a number of places
	//based on https://codereview.stackexchange.com/a/109219
	do {
		value *= 10;
	    *p++ = '0' + (value >> 16);
	    value &= ((1 << 16) - 1);
	} while (value > 0 && places--) ;
	*p++ = ',';
	*pp = p;
}
