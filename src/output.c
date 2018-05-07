
#include "main.h"

//an fft of 512 gives us 256 buckets of frequency info. at 20khz, each has ~38Hz
//we don't really want all 256 buckets of frequency info
//if we compressed this linearly, we'd lose a lot of low/mid tone info
//also, in order to capture low frequency stuff below 38Hz would require a much larger fft
//so we can combine a downsampled 400hz using a smaller fft for low frequency stuff with the 20khz stuff

//the first 6 buckets are dedicated to low frequency audio from 12.5-162.5 Hz
const uint8_t lowFrequencyMap[6] = {
		3, 4, 6, 8, 10, 13
};
//the next 26 buckets are dedicated to higher frequency audio from 195Hz to just under the nyquist limit of 10khz
const uint8_t highFrequencyMap[26] = {
		5, 6, 8, 10, 12, 15, 18, 22, 25, 30, 35, 40, 46, 53, 61, 70, 80, 92, 105, 119, 136, 154, 175, 199, 225, 255,
};


char outBuffer[100];
int outBufferLen;

#define WRITEOUT(v) {memcpy(out, &v, sizeof(v)); out+= sizeof(v);}

/*
 * Takes a real input, applies Hann window, calculates energyAverage
 * out must be the the same size as in as it is used for the imaginary part
 * after returning, the first half is filled with bucket magnitudes
 * magnitude is multiplied by 16 and saturates at 16 bits
 * m = log2(n)
 */
void fftRealWindowedMagnitude(int16_t * in, uint16_t * out, int m, uint16_t * energyAverage) {
	int16_t * imag = (int16_t *) out; //borrow out for imaginary part
	int n = 1 << m;

	//init with zeros and calculate average (DC offset)
	for (int i = 0; i < n; i++) {
		imag[i] = 0;
	}
	uint32_t energyTotal = 0;
	for (int i = 0; i < n; i++) {
		energyTotal += abs(in[i]);

		//apply the hann windowing function, borrowing Sinewave LUT from fix_fft
		//the positive portion of Sinewave ranges from index 0-512
		// (i * 512) / n  == (i * 512) >> m
		int si = (i * 512) >> m ;
		in[i] = (Sinewave[si] * in[i]) >> 16;
	}
	*energyAverage = energyTotal >> m;

	//run the FFT (runs in place, overwriting both in and imag)
	fix_fft(in, imag, m, 0);

	//calculate the magnitude and store in out for only the first half
	int halfN = n>>1;
	for (int i = 0; i < halfN; i++) {
		//using the fix16_sqrt gives us a bit more resolution as we get
		//8 bits more using this over an integer sqrt
		int32_t t = fix16_sqrt(imag[i] * imag[i] + in[i] * in[i]);

		//we can't keep all those extra bits, but 4 of 8 seems like a good value
		//as this only overloads a little and only for REALLY LOUD inputs
		t >>= 4;
		if (t > 0xffff)
			t = 0xffff;
		out[i] = t;
	}
}

void processSensorData(int16_t * audioBuffer, int16_t * audio400HzBuffer, volatile uint16_t adcBuffer[7], volatile int16_t accelerometer[3]) {
	uint16_t magnitude[HIGH_N]; //temp and output from the fft
	uint16_t lowEnergy;
	uint16_t energyAverage;
	int maxFrequencyIndex = 0;
	uint16_t maxFrequencyMagnitude = 0;
	uint16_t maxFrequencyHz;
	char * out = outBuffer;

	//start making output buffer
	WRITEOUT("SB1.0");

	//do the low frequency stuff
	fftRealWindowedMagnitude(audio400HzBuffer, &magnitude[0], LOW_NLOG2, &lowEnergy);
	//write out low frequency stuff
	for (int i = 0, k = lowFrequencyMap[0]; i < 6; i++) {
		int top = lowFrequencyMap[i] + 1;
		int size = top - k;
		uint16_t max = 0;
		for (; k < top; k++) {
			max = magnitude[k] > max ? magnitude[k] : max;
		}
		WRITEOUT(max);
	}

	//do high frequency stuff
	fftRealWindowedMagnitude(audioBuffer, &magnitude[0], HIGH_NLOG2, &energyAverage);

	//run through and get maxFrequency info
	for (int i = 1; i < HIGH_N/2; i++) {
		if (magnitude[i] > maxFrequencyMagnitude) {
			maxFrequencyMagnitude = magnitude[i];
			maxFrequencyIndex = i;
		}
	}

	//write out high frequency stuff
	for (int i = 0, k = highFrequencyMap[0]; i < 26; i++) {
		int top = highFrequencyMap[i] + 1;
		int size = top - k;
		uint16_t max = 0;
		for (; k < top; k++) {
			max = magnitude[k] > max ? magnitude[k] : max;
		}
		WRITEOUT(max);
	}

	WRITEOUT(energyAverage);
	WRITEOUT(maxFrequencyMagnitude);
	maxFrequencyHz = (20000 * (int32_t)maxFrequencyIndex) / 512; //or 39.0625 per bin
	WRITEOUT(maxFrequencyHz);

	for (int i = 0; i < 3; i++) {
		int16_t v = accelerometer[i];
		WRITEOUT(v);
	}

	for (int i = 1; i < 7; i++) {
		uint16_t v = adcBuffer[i]<<4;
		WRITEOUT(v);
	}

	WRITEOUT("END");

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
