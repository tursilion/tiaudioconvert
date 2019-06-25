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
	255,191,159,143,135,131,129,
	128,	//50,
	127,125,121,113,97,65,30,0		// bottom half steps faster than top to try and equalize it
//      ^^^                 ^^   new entries, untested, todo above

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
	int nLast = 0x0f;

	// testing says this is marginally better than just (nTmp>>4)
	// The hand-tuned volume table helps a lot, but I wonder if we
	// could make that better? There's a lot of quantize error going
	// down to 4 bit even if the logarithmic scale wasn't there, though.
	for (idx=0; idx < 16; idx++) {
		if (abs(sms_volume_table[idx]-nTmp) < nDistance) {
			nBest = idx;
			nDistance = abs(sms_volume_table[idx]-nTmp);
		}
	}

	// return the index of the best match
	nLast=nBest;
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
	// emulate high frequency output
	int sign = 1;
	// audio is 13485Hz, so let's just do a 4 times that for frequency - 53940Hz
	// I think the chip can even pull that off... it's close anyway.
	for (int idx=0; idx<outpos; idx++) {
		int out = real_sms_volume_table[waveout[idx]&0x0f]>>1;
		fputc(out, fp);
//		fputc(out, fp);
//		fputc(-out, fp);
//		fputc(-out, fp);
	}
	fclose(fp);

	free(wavein);
	free(waveout);

	return 0;
}

