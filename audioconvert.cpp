// audioconvert.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <memory.h>

unsigned char *wavein, *waveout;

// 8 bit volume table (because 8-bit unsigned is being unwrapped)
// These values are manually tweaked to give a more pleasing conversion,
// the output uses the real table for a better sample of what it might
// sound like.
// TODO: the duplication of 127 is a bug that costs us some resolution
// Also, there are only 15 entries, so mute is not considered, and 16 values are tested (probably 0?)
// I've added the missing 0 and changed the second 127 to 125, but this is untested...
// (Confirmed that tools are using the entire range, except the missing index 9, which was the second 127)
int sms_volume_table[16]={
#if 0
    // hacky 2 bit instead of 4 bit version
    255,255,255,255,128,128,128,
	128,	//50,
	65,65,65,65,0,0,0,0		// bottom half steps faster than top to try and equalize it

#else

    255,191,159,143,135,131,129,
	128,	//50,
	127,125,121,113,97,65,30,0		// bottom half steps faster than top to try and equalize it
//      ^^^                 ^^   new entries, untested, todo above
#endif

//	240,215,190,170,155,140,129,
//	128,	//50,
//	127,120,100,85,70,55,40,30		// bottom half steps faster than top to try and equalize it
};
int real_sms_volume_table[16]={
	254,202,160,128,100,80,64,
	50,
	40,32,24,20,16,12,10,0
};
// input: 8 bit unsigned audio (centers on 128)
// output: 15 (silent) to 0 (max)
int maplevel(int nTmp) {
	int nBest = -1;
	int nDistance = INT_MAX;
	int idx;

	// testing says this is marginally better than just (nTmp>>4)
	// The hand-tuned volume table helps a /lot/, but I wonder if we
	// could make that better? There's a lot of quantize error going
	// down to 4 bit even if the logarithmic scale wasn't there, though.
#if 0
    // this one discards the negative from the input wave and makes it all positive
    // that results in very corrupted waveforms, regardless of mapping
    if ((nTmp >= 126) && (nTmp <= 130)) {
        nTmp = 0;
    } else if (nTmp > 128) {
        nTmp = (nTmp-128) * 2;
    } else {
        nTmp = (128-nTmp)*2;
    }
    for (idx=0; idx < 16; idx++) {
		if (abs(real_sms_volume_table[idx]-nTmp) < nDistance) {
			nBest = idx;
			nDistance = abs(real_sms_volume_table[idx]-nTmp);
		}
	}
#elif 0
    // this is like the above, but fully discards the negative part of the waveform,
    // looking only at the positive. The audio is recognizable, but clearly corrupted
    if ((nTmp >= 126) && (nTmp <= 130)) {
        nTmp = 0;
    } else if (nTmp > 128) {
        nTmp = (nTmp-128) * 2;
    } else {
        // negative
        nTmp = 0;
    }
    for (idx=0; idx < 16; idx++) {
		if (abs(real_sms_volume_table[idx]-nTmp) < nDistance) {
			nBest = idx;
			nDistance = abs(real_sms_volume_table[idx]-nTmp);
		}
	}
#elif 0
    // Similar to the first one, but when the waveform changes sign,
    // we drop to zero for one frame
    // Adds a rather strange distortion to the sound
    static int nLast = 0;
    if ((nTmp >= 126) && (nTmp <= 130)) {
        nTmp = 0;
        nLast = 0;
    } else if (nTmp > 128) {
        if (nLast >= 0) {
            nLast = nTmp - 128;
            nTmp = (nTmp-128) * 2;
        } else {
            nTmp = 0;
            nLast = 0;
        }
    } else {
        // negative
        if (nLast <= 0) {
            nLast = nTmp - 128;
            nTmp = (128-nTmp)*2;
        } else {
            nTmp = 0;
            nLast = 0;
        }
    }
    for (idx=0; idx < 16; idx++) {
		if (abs(real_sms_volume_table[idx]-nTmp) < nDistance) {
			nBest = idx;
			nDistance = abs(real_sms_volume_table[idx]-nTmp);
		}
	}

#elif 1
    // this is the best result so far, it uses a hand-tuned volume table scaled for counts around center
    for (idx=0; idx < 16; idx++) {
		if (abs(sms_volume_table[idx]-nTmp) < nDistance) {
			nBest = idx;
			nDistance = abs(sms_volume_table[idx]-nTmp);
		}
	}
#elif 0
    // this version attempts to map only the magnitude of the change
    // this uses the real table. However, the output is pure noise with
    // barely a semblance of the original
    static int lastVal = -1;
    static int lastOut = -1;

    if (lastVal == -1) {
        // just choose a relative starting point
        for (idx=0; idx < 16; idx++) {
		    if (abs(sms_volume_table[idx]-nTmp) < nDistance) {
			    nBest = idx;
			    nDistance = abs(real_sms_volume_table[idx]-nTmp);
		    }
	    }
    } else {
        // find the closest difference
        int delta = abs(lastVal - nTmp);    // desired delta
        for (int idx=0; idx<16; idx++) {
            int tst = abs(real_sms_volume_table[idx] - real_sms_volume_table[lastOut]);
            int dst = abs(tst-delta);
            if (dst < nDistance) {
                nBest = idx;
                nDistance = dst;
            }
        }
    }
    lastVal = nTmp;
    lastOut = nBest;

#else
    // this uses the real volume table and only works on very few samples
    for (idx=0; idx < 16; idx++) {
		if (abs(real_sms_volume_table[idx]-nTmp) < nDistance) {
			nBest = idx;
			nDistance = abs(real_sms_volume_table[idx]-nTmp);
		}
	}
#endif

	// return the index of the best match
	return nBest;
}

int main(int argc, char* argv[])
{
	int DataSize = 0;

	// open input WAV file and skip the header, because I don't care
	if (argc < 3) {
		printf("audioconvert <input.wav> <output.bin>\n");
		return -1;
	}

	FILE *fp = fopen(argv[1], "rb");
	if (NULL == fp) {
		printf("Can't open input file\n");
		return -1;
	}
	fseek(fp, 0, SEEK_END);
	DataSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	printf("Got data size of %d bytes\n", DataSize);
	wavein=(unsigned char*)malloc(DataSize+1);		// always good to leave a little slack ;)
	waveout=(unsigned char*)malloc(DataSize+1);

	int insize = fread(wavein, 1, DataSize, fp);
	fclose(fp);
	if (insize < 44) {
		printf("Too small to be RIFF WAV. Or at least worth considering.\n");
		return -1;
	}
	// find the data chunk
	if (memcmp(&wavein[0], "RIFF", 4)) {
		printf("Not RIFF\n");
		return -1;
	}
	if (memcmp(&wavein[8], "WAVE", 4)) {
		printf("Not WAVE\n");
		return -1;
	}
	int wavepos = 12;
	while (memcmp(&wavein[wavepos], "fmt ", 4)) {
		int step = (wavein[wavepos+4])+(wavein[wavepos+5]<<8)+(wavein[wavepos+6]<<16)+(wavein[wavepos+7]<<24);
		wavepos+=8+step;
		if (wavepos >= insize-8) {
			printf("Can't find data chunk.\n");
			return -1;
		}
	}
	if ((wavein[wavepos+8]!=1)&&(wavein[wavepos+9]!=0)) {
		printf("Not PCM\n");
		return -1;
	}
	if ((wavein[wavepos+10]!=1)&&(wavein[wavepos+11]!=0)) {
		printf("Not Mono\n");
		return -1;
	}
	if ((wavein[wavepos+22]!=8)&&(wavein[wavepos+9]!=0)) {
		printf("Not 8-bit\n");
		return -1;
	}

	wavepos = 12;
	while (memcmp(&wavein[wavepos], "data", 4)) {
		int step = (wavein[wavepos+4])+(wavein[wavepos+5]<<8)+(wavein[wavepos+6]<<16)+(wavein[wavepos+7]<<24);
		wavepos+=8+step;
		if (wavepos >= insize-8) {
			printf("Can't find data chunk.\n");
			return -1;
		}
	}

	int countdown = (wavein[wavepos+4])+(wavein[wavepos+5]<<8)+(wavein[wavepos+6]<<16)+(wavein[wavepos+7]<<24);
	if (countdown > insize) {
		printf("Header reports invalid size\n");
		return -1;
	}
	wavepos+=8;
	int outpos = 0;

	while (countdown--) {
		waveout[outpos++] = maplevel(wavein[wavepos++]) | 0x90;
	}

	fp=fopen(argv[2], "wb");
	if (NULL == fp) {
		printf("Can't open output file\n");
		return -1;
	}
	fwrite(waveout, 1, outpos, fp);
	fclose(fp);

	// write a test file too - 8 bit raw signed
	fp=fopen("testout.raw", "wb");
	if (NULL == fp) {
		printf("Can't open output file\n");
		return -1;
	}
#if 0
	// emulate square wave output at 2mult times input frequency
    int mult = 16;
	for (int idx=0; idx<outpos; idx++) {
		int outHi = real_sms_volume_table[waveout[idx]&0x0f]>>1;
		int outLo = real_sms_volume_table[15-(waveout[idx]&0x0f)]>>1;
        for (int j=0; j<mult; ++j) {
            fputc(outHi, fp);
      		fputc(outLo, fp);
        }
	}
#else
	for (int idx=0; idx<outpos; idx++) {
		int out = real_sms_volume_table[waveout[idx]&0x0f]>>1;
		fputc(out, fp);
	}
#endif
	fclose(fp);

	free(wavein);
	free(waveout);

	return 0;
}

