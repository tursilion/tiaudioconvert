// audioconvert.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <memory.h>

unsigned char *wavein, *waveout;
bool useRealTable = false;  // for generic samples, my scaled table sounds better. for precisely adjusted samples, the real one does.

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
#elif 0
    // everyone says this should work, but I think it only works on chips that
    // can manage a flat line output, like the AY. But here we attempt to manage
    // all three voices. The player is not currently set up for this, I'm
    // testing via hacks
    // But the idea is that there is a much better range available by adding
    // all three voices together. We still need to piecemeal it with this
    // player, but it's worth an experiment.
    // Okay, this sounds awful even when the channels are in sync, and is likely to
    // be worse if they are not. The square wave carrier causes too much noise
    static int nLast1 = 15, nLast2 = 15, nLast3 = 15;
    int tmp_sms_volume_table[16];

    for (int idx = 0; idx < 16; ++idx) {
        tmp_sms_volume_table[idx] = real_sms_volume_table[idx]/3;
    }

    // we pick the voices in order, so that voice 1 is the biggest contributor, then 2, then 3
    // we assume it's always additive, which I think is where this falls down on the TI.
    // because of the order of the table, we just take the first one that's less than
    int v1=15, v2=15, v3=15;    // mute in case not found
    for (idx=0; idx < 16; idx++) {
        if (tmp_sms_volume_table[idx] <= nTmp) {
            v1 = idx;
            break;
        }
    }
    for (idx=0; idx < 16; idx++) {
        if (tmp_sms_volume_table[idx]+tmp_sms_volume_table[v1] <= nTmp) {
            v2 = idx;
            break;
        }
    }
    for (idx=0; idx < 16; idx++) {
        if (tmp_sms_volume_table[idx]+tmp_sms_volume_table[v1]+tmp_sms_volume_table[v2] <= nTmp) {
            v3 = idx;
            break;
        }
    }

    // now decide which one to return by seeing which gets us closest
    int oldOut = tmp_sms_volume_table[nLast1]+tmp_sms_volume_table[nLast2]+tmp_sms_volume_table[nLast3];
    int diff1 = abs((oldOut-tmp_sms_volume_table[nLast1]+tmp_sms_volume_table[v1])-nTmp);
    int diff2 = abs((oldOut-tmp_sms_volume_table[nLast2]+tmp_sms_volume_table[v2])-nTmp);
    int diff3 = abs((oldOut-tmp_sms_volume_table[nLast3]+tmp_sms_volume_table[v3])-nTmp);
    if ((diff1 <= diff2) && (diff1 <= diff3)) {
        nLast1 = v1;
        return v1|0x90;
    }
    if ((diff2 <= diff1) && (diff2 <= diff3)) {
        nLast2 = v2;
        return v2|0xB0;
    }
    nLast3 = v3;
    return v3|0xD0;


#elif 1
    if (useRealTable) {
        // this uses the real volume table and only works on very few samples
        for (idx=0; idx < 16; idx++) {
		    if (abs(real_sms_volume_table[idx]-nTmp) < nDistance) {
			    nBest = idx;
			    nDistance = abs(real_sms_volume_table[idx]-nTmp);
		    }
	    }
    } else {
        // this is the best result so far, it uses a hand-tuned volume table scaled for counts around center
        for (idx=0; idx < 16; idx++) {
		    if (abs(sms_volume_table[idx]-nTmp) < nDistance) {
			    nBest = idx;
			    nDistance = abs(sms_volume_table[idx]-nTmp);
		    }
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

    printf("audioconvert 20251121\n");
	// open input WAV file and skip the header, because I don't care
    // must already be 8 bit mono - samplerate will not be changed
	if (argc < 3) {
        // optionally pass 'pack' to pack 2 nibbles to a byte instead
        // of appending the 0x90 sound command. Most significant nibble plays first.
		printf("audioconvert <input.wav> <output.bin> [pack] [real]\n");
        printf("Sample rate is preserved. 'pack' packs 2 nibbles to a byte, else it uses 0x90.\n");
        printf("'real' uses the real mapping instead of the scaled mapping. This only works better\n");
        printf("for samples that have been cleaned up and volume maximized, most of the time.\n");
		return -1;
	}
    bool pack = false;
    for (int i=3; i<argc; ++i) {
        if (0 == strcmp(argv[i], "pack")) {
            printf("Pack nibbles\n");
            pack = true;
        }
        if (0 == strcmp(argv[i], "real")) {
            printf("Real table\n");
            useRealTable = true;
        }
    }
    int packed = 0;

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
	if ((countdown > insize)||(countdown > DataSize)) {
		printf("Header reports invalid size\n");
		return -1;
	}
	wavepos+=8;
	int outpos = 0;

    // if we're packing, discard last odd sample
    if ((packed) && (countdown&1)) --countdown;

	while (countdown--) {
        int ret = maplevel(wavein[wavepos++]);
        if (pack) {
            // pack two nibbles
            packed <<= 4;
            packed |= (ret&0x0f);
            if ((countdown&1)==0) {
                waveout[outpos++] = packed&0xff;
            }
        } else {
            // append sound command instead
            if (ret < 0x80) ret |= 0x90;    // accomodate maplevel maybe returning a channel command
	    	waveout[outpos++] = ret;
        }
	}

	fp=fopen(argv[2], "wb");
	if (NULL == fp) {
		printf("Can't open output file\n");
		return -1;
	}
	fwrite(waveout, 1, outpos, fp);
	fclose(fp);

	// write a test file too - 8 bit raw signed
    char outname[256];
    memset(outname, 0, sizeof(outname));
    strncpy(outname, argv[1], sizeof(outname)-16);
    strcat(outname, "testout.raw");
	fp=fopen(outname, "wb");
	if (NULL == fp) {
		printf("Can't open output file\n");
		return -1;
	}
#if 0
	// emulate square wave output at 2mult times input frequency
    int mult = 2;
    int l1=0,l2=0,l3=0;
	for (int idx=0; idx<outpos; idx++) {
        int c = waveout[idx];
        switch (c&0xc0) {
            case 0x80: l1 = c&0x0f; break;
            case 0xa0: l2 = c&0x0f; break;
            case 0xc0: l3 = c&0x0f; break;
        }
		int outHi = real_sms_volume_table[l1] + real_sms_volume_table[l2] + real_sms_volume_table[l3] / 3;
		int outLo = 128 - outHi;
        for (int j=0; j<mult; ++j) {
            fputc(outHi, fp);
      		fputc(outLo, fp);
        }
	}
#else
	for (int idx=0; idx<outpos; idx++) {
        if (packed) {
            // two at a time
            int out = real_sms_volume_table[(waveout[idx]&0xf0)>>4]>>1;
	    	fputc(out, fp);
            out = real_sms_volume_table[waveout[idx]&0xf]>>1;
	    	fputc(out, fp);
        } else {
    		int out = real_sms_volume_table[waveout[idx]&0x0f]>>1;
	    	fputc(out, fp);
        }
	}
#endif
	fclose(fp);

	free(wavein);
	free(waveout);

	return 0;
}

