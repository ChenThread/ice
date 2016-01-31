/**
 * Copyright (c) 2015-2016 Adrian "asie" Siekierka, Ben "GreaseMonkey" Russell
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated 
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 * to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of 
 * the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO 
 * THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF 
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE.
 */

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <assert.h>

#include "argparse/argparse.h"

#define VW 160
#define VH 50

FILE *infp; // Fi Ne Si Te
FILE *fp;
FILE *dumpfp;

int gpu_max_budget;
int verbose = 0;
int frameno = 0;

int palr[256];
int palg[256];
int palb[256];

// crazy internal variables

uint8_t rawinbuf[VH][VW][3];
uint8_t inbuf[VH][VW];
uint8_t lastbuf[VH][VW];
uint8_t agebuf[VH][VW];

uint8_t exactnow = 0;

uint32_t ccount[256];
uint32_t corder[256];

uint8_t cmatch2res[65536];

typedef struct fillop {
	int bx, by, bw, bh, col;
	int type;
} fillop;

struct oplist {
	fillop* fillop;
	uint8_t valid;
	struct oplist* next;
};

typedef struct oplist oplist;

fillop gpuoplist[256];
uint8_t gpu_cu[256];
int gpu_ops = 0;
int gpu_budget = 0;
int gpu_sets = 0;
int gpu_fills = 0;
int gpu_cchanges = 0;

int rgb_to_oct(int v)
{
	if(v < 16) return 0777;
	assert(v >= 16 && v <= 255);
	v -= 16;

	int r = v%6;
	int g = (v/6)%8;
	int b = (v/6)/8;

	assert(r >= 0 && r < 5);
	assert(g >= 0 && g < 8);
	assert(b >= 0 && b < 6);

	return (r<<6)|(g<<3)|b;
}

int f_sort_corder(const void *av, const void *bv)
{
	int32_t ai = *(int32_t *)av;
	int32_t bi = *(int32_t *)bv;

	return -(ccount[ai] - ccount[bi]);
}

static float cmatch3(int r1i, int g1i, int b1i, int r2i, int g2i, int b2i)
{
	float r1 = r1i / 255.0f;
	float r2 = r2i / 255.0f;
	float g1 = g1i / 255.0f;
	float g2 = g2i / 255.0f;
	float b1 = b1i / 255.0f;
	float b2 = b2i / 255.0f;

	float y1 = 0.299f * r1 + 0.587f * g1 + 0.114f * b1;
	float u1 = -0.147f * r1 - 0.289f * g1 + 0.436f * b1;
	float v1 = 0.615f * r1 - 0.515f * g1 - 0.100f * b1;
	float y2 = 0.299f * r2 + 0.587f * g2 + 0.114f * b2;
	float u2 = -0.147f * r2 - 0.289f * g2 + 0.436f * b2;
	float v2 = 0.615f * r2 - 0.515f * g2 - 0.100f * b2;

	return sqrt((y2-y1) * (y2-y1) + (u2-u1) * (u2-u1) + (v2-v1) * (v2-v1));
}

static int cmatch2a(int x, int y, int c1, int c2)
{
	//return c1 == c2;
	//if(agebuf[y][x] > 10) return c1 == c2;

	int r1 = c1%6;
	int g1 = (c1/6)%8;
	int b1 = (c1/6)/8;

	int r2 = c2%6;
	int g2 = (c2/6)%8;
	int b2 = (c2/6)/8;

	int d = 0
		+(r1 > r2 ? r1-r2 : r2-r1)
		+(g1 > g2 ? g1-g2 : g2-g1)
		+(b1 > b2 ? b1-b2 : b2-b1);
	
	return d < 2;
}

static int cmatch2(int x, int y, int c1, int c2)
{
	if (c1 == c2) return 1;
	//return c1 == c2;
	//if(agebuf[y][x] > 10) return c1 == c2;

	return exactnow > 0 ? (c1 == c2) : cmatch2res[(c1 << 8) | c2];
}

static int cmatch(int c1, int c2)
{
	return c1 == c2;
}

void gpu_start()
{
	for (int i = 0; i < 256; i++) {
		gpu_cu[i] = 0;
	}
	gpu_budget = 0;
	gpu_cchanges = 0;
	gpu_sets = 0;
	gpu_fills = 0;
	gpu_ops = 0;
}

void gpu_emit()
{
	int i, colors_changed;
	oplist* colorlist[256];
	int len[256];

	for (i = 0; i < 256; i++)
	{
		colorlist[i] = NULL;
		len[i] = 0;
	}

	gpu_cchanges = 0;

	for (i = 0; i < gpu_ops; i++)
	{
		fillop* op = &gpuoplist[i];

		if (colorlist[op->col] == NULL)
		{
			gpu_cchanges++;
			colorlist[op->col] = malloc(sizeof(oplist));
			colorlist[op->col]->valid = 0;
		}

		oplist* list = colorlist[op->col];
		while (list->valid)
		{
			list = list->next;
		}
		list->fillop = op;
		list->valid = 1;
		list->next = malloc(sizeof(oplist));
		list->next->valid = 0;
	}

	for (i = 0; i < 256; i++)
		if (colorlist[i] != NULL)
		{
			if (fp != NULL)
			{
				fputc(0, fp);
				fputc(i, fp);
			}
			colors_changed++;
			oplist* list = colorlist[i];
			oplist* prev = list;
			while (list->valid)
			{
				fillop* op = list->fillop;
				if(fp != NULL)
				{
					fputc(((op->type - 1) << 6) | op->bh, fp);
					fputc(op->by + 1, fp);
					fputc(op->bx + 1, fp);
					fputc(op->bw, fp);
				}
				list = list->next;
				free(prev);
				prev = list;
			}
		}

	if(fp != NULL) fputc(0xFF, fp);
}

void gpu_fill(int bx, int by, int bw, int bh, int col)
{
	int x, y;
	int diff = 0;

	for(y = 0; y < bh; y++)
	for(x = 0; x < bw; x++)
	{
		assert(x+bx >= 0 && x+bx < VW);
		assert(y+by >= 0 && y+by < VH);

		if (lastbuf[y+by][x+bx] != col)
		{
			lastbuf[y+by][x+bx] = col;
			diff++;
		}
	}

	if (diff == 0) return;

	fillop* op = &gpuoplist[gpu_ops++];

	op->bx = bx;
	op->by = by;
	op->bw = bw;
	op->bh = bh;
	op->col = col;

	if((bh == 1 || bw == 1))
		op->type = 1;
	else
		op->type = 2;

	if (gpu_cu[op->col] == 0) {
		gpu_budget += 2;
		gpu_cu[op->col] = 1;
	}

	if (op->type == 1) {
		gpu_sets++; gpu_budget+=1;
	}
	else {
		gpu_fills++; gpu_budget+=2;
	}
	
	gpu_cchanges++;
}

static inline int convert_age(int x, int y)
{
	int v = agebuf[y][x];
	return v;
}

static inline int convert_age_hits(int x, int y)
{
	return 1;
	int v = agebuf[y][x];

	v -= 8;
	if(v < 1) return 1;
	v += 1;
	return v;
	/*
	int c1 = (int)inbuf[y][x];
	int c2 = (int)lastbuf[y][x];
	int r1 = c1%6;
	int g1 = (c1/6)%8;
	int b1 = (c1/6)/8;

	int r2 = c2%6;
	int g2 = (c2/6)%8;
	int b2 = (c2/6)/8;

	int diff = 0
		+(r1 > r2 ? r1-r2 : r2-r1)
		+(g1 > g2 ? g1-g2 : g2-g1)
		+(b1 > b2 ? b1-b2 : b2-b1);

	diff -= 7;
	if(diff < 1) return 1;
	return diff*diff;
	*/
}

void algo_1(int variant)
{
	int x, y, i, j;

	gpu_start();

	for(j = 0; j < 16; j++)
	{
		if(gpu_budget > gpu_max_budget)
			break;

		exactnow = (j >= 8) ? 1 : 0;

		// sort by colour count
		for(i = 0; i < 256; i++)
		{
			ccount[i] = 0;
			corder[i] = i;
		}

		for(y = 0; y < VH; y++)
		for(x = 0; x < VW; x++)
		if(!cmatch2(x, y, inbuf[y][x], lastbuf[y][x]))
			ccount[inbuf[y][x]] += convert_age(x, y);

		qsort(corder, 256, sizeof(uint32_t), f_sort_corder);

		// do base fills
		for(i = 0; i < 28; i++)
		{
			if(gpu_budget > gpu_max_budget)
				break;

			if(ccount[corder[i]] == 0) continue;
			if(corder[i] < 0) continue;

			int xn = VW;
			int yn = VH;
			int xp = 0;
			int yp = 0;
			int hits = 0;
			int unhits = 0;

			// build initial box
			for(y = 0; y < VH; y++)
			for(x = 0; x < VW; x++)
			{
				if(cmatch(inbuf[y][x], corder[i]) && !cmatch2(x, y, lastbuf[y][x], corder[i]))
				//	hits++;
				//if(inbuf[y][x] == corder[i] && lastbuf[y][x] != corder[i])
				{
					hits += convert_age_hits(x, y);
					if(xn > x) xn = x;
					if(yn > y) yn = y;
					if(xp < x) xp = x;
					if(yp < y) yp = y;
				}
			}

			// calc unhits
			unhits = 0;
			for(y = yn; y <= yp; y++)
			for(x = xn; x <= xp; x++)
			{
				//if(!cmatch(lastbuf[y][x], inbuf[y][x]))
				if(!cmatch2(x, y, lastbuf[y][x], corder[i]))
				unhits += convert_age_hits(x, y);
			}

			// if everything is fine, skip
			if(hits == 0) continue;
			if(unhits == 0) continue;
			//fprintf(stderr, "%i %i %03o\n", hits, unhits, rgb_to_oct(corder[i]));

			if(((variant>>0)&0xF) == 1)
			{
				int begx = -1;
				int begy = -1;

				// find a point for a cluster start
				for(x = xn; x <= xp; x++)
				for(y = yn; y <= yp; y++)
				if(cmatch(inbuf[yn][x], corder[i]) && !cmatch(lastbuf[yn][x], corder[i]))
				{
					begx = x;
					begy = y;
					goto a1v1_got_cluster_start; // far break
				}

				a1v1_got_cluster_start:
				if(begx == -1)
					continue;

				// now cluster
				xn = xp = begx;
				yn = yp = begy;
				//fprintf(stderr, "C %i %i\n", xn, yn);

				for(;;)
				{
					for(x = xn; x <= xp; x++)
					if(cmatch(inbuf[yn][x], corder[i]) && !cmatch(lastbuf[yn][x], corder[i]))
						;

					break;
				}

				// TODO: cluster!

			} else {
				// reduce box
				//while((xp-xn+1) > 0 && (yp-yn+1) > 0 && (hits*100)/((xp-xn+1)*(yp-yn+1)) < 70)
				while((xp-xn+1) > 0 && (yp-yn+1) > 0 && (hits*100)/unhits < 60)
				{
					int remxn = 0;
					int remyn = 0;
					int remxp = 0;
					int remyp = 0;
					int spcxn = 0;
					int spcyn = 0;
					int spcxp = 0;
					int spcyp = 0;

					for(x = xn; x <= xp; x++)
					{
						if(cmatch(inbuf[yn][x], corder[i]) && !cmatch2(x, yn, lastbuf[yn][x], corder[i]))
							remyn += convert_age_hits(x, yn);
						if(cmatch(inbuf[yp][x], corder[i]) && !cmatch2(x, yp, lastbuf[yp][x], corder[i]))
							remyp += convert_age_hits(x, yp);
						if(!cmatch2(x, yn, lastbuf[yn][x], corder[i]))
							spcyn += convert_age_hits(x, yn);
						if(!cmatch2(x, yp, lastbuf[yp][x], corder[i]))
							spcyp += convert_age_hits(x, yp);
					}

					for(y = yn; y <= yp; y++)
					{
						if(cmatch(inbuf[y][xn], corder[i]) && !cmatch2(xn, y, lastbuf[y][xn], corder[i]))
							remxn += convert_age_hits(xn, y);
						if(cmatch(inbuf[y][xp], corder[i]) && !cmatch2(xp, y, lastbuf[y][xp], corder[i]))
							remxp += convert_age_hits(xp, y);
						if(!cmatch2(xn, y, lastbuf[y][xn], corder[i]))
							spcxn += convert_age_hits(xn, y);
						if(!cmatch2(xp, y, lastbuf[y][xp], corder[i]))
							spcxp += convert_age_hits(xp, y);
					}

					if(remxn < remxp && remxn < remyn && remxn < remyp)
						{ xn++; hits -= remxn; unhits -= spcxn; }
					else if(remxp < remyn && remxp < remyp)
						{ xp--; hits -= remxp; unhits -= spcxp; }
					else if(remyn < remyp)
						{ yn++; hits -= remyn; unhits -= spcyn; }
					else
						{ yp--; hits -= remyp; unhits -= spcyp; }
				}
			}

			if(xn <= xp && yn <= yp)
				gpu_fill(xn, yn, xp-xn+1, yp-yn+1, corder[i]);
		}
	}

	gpu_emit();
	frameno++;

	fprintf(stderr, "[%d] Call budget: %d/%d (%d sets, %d fills)\n", frameno, gpu_budget, gpu_max_budget + 4, gpu_sets, gpu_fills);
}

static const char *const usage[] = {
	"ice [options] -i input -o output",
	NULL,
};

int main(int argc, const char *argv[])
{
	int x, y, i, j;
	int gpu_tier;
	char *infn = NULL, *outfn = NULL, *dumpfn = NULL;

	struct argparse argparse;
	struct argparse_option options[] = {
		OPT_HELP(),
		OPT_STRING('i', "input", &infn, "input filename, in form of raw RGB24 frames"),
		OPT_STRING('o', "output", &outfn, "output filename, in form of ICE-encoded data"),
		OPT_STRING('d', "dump", &dumpfn, "debug data filename, in form of raw RGB24 frames"),
		OPT_END()
	};

	argparse_init(&argparse, options, usage, 0);
	argparse_describe(&argparse, "\nICE OpenComputers codec encoder", "");
	argc = argparse_parse(&argparse, argc, argv);
	if (infn == NULL)
	{
		printf("Error: Input filename not specified!\n");
		return 1;
	}
	if (outfn == NULL)
	{
		printf("Error: Output filename not specified!\n");
		return 1;
	}

	// Keep in mind that a Tier 3 GPU has 256 "budget points". 
	// We only verify whether the operation passed the budget
	// after the fact. Our most expensive single operation costs
	// 2 points, so we can have a max of at most 254 - but we
	// subtract another 2 just to be safe.
	gpu_max_budget = 252;

	infp = strcmp(infn, "-") == 0 ? stdin : fopen(infn, "rb");
	if(infp == NULL)
	{
		perror(infn);
		return 1;
	}

	fp = strcmp(outfn, "-") == 0 ? stdout : fopen(outfn, "wb");
	if(fp == NULL)
	{
		perror(outfn);
		return 1;
	}

	if(dumpfn != NULL)
	{
		dumpfp = strcmp(dumpfn, "-") == 0 ? stdout : fopen(dumpfn, "wb");
		if(dumpfp == NULL)
		{
			perror(dumpfn);
			return 1;
		}
	}

	fputc(VW, fp);
	fputc(VH, fp);

	memset(lastbuf, 16, VW*VH);
	memset(agebuf, 2, VW*VH);

	for(i = 0; i < 256; i++)
	{
		int v = i;
		int r, g, b;
		if (v >= 16) {
			v -= 16;
			r = v%6;
			g = (v/6)%8;
			b = (v/6)/8;
			r *= 255; g *= 255; b *= 255;
			r +=   2; g +=   3; b +=   2;
			r /=   5; g /=   7; b /=   4;
		} else {
			r = v*255/15;
			g = v*255/15;
			b = v*255/15;
		}
		assert(r >= 0 && r <= 255);
		assert(g >= 0 && g <= 255);
		assert(b >= 0 && b <= 255);
		palr[i] = r;
		palg[i] = g;
		palb[i] = b;
	}
	for (i = 0; i < 256; i++)
	{
		for(j = 0; j < 256; j++)
		{	
			if (j < i) {
				cmatch2res[(j << 8) | i] = cmatch2res[(i << 8) | j];
			} else {
				cmatch2res[(j << 8) | i] = cmatch3(palr[i], palg[i], palb[i], palr[j], palg[j], palb[j]) < 0.1f; // 0.01 * 255
			}
			/*if (cmatch2res[(j << 8) | i] == 0) {
				printf("%d %d %d %f : %d\n", i, j, cmatch2res[(j << 8) | i],
				cmatch3(palr[i], palg[i], palb[i], palr[j], palg[j], palb[j]), cmatch2a(0, 0, i, j)
);
			}*/
		}
	}

	for(;;)
	{
		// fetch
		if(fread(rawinbuf, VW*VH*3, 1, infp) <= 0)
		{
			//for(;;) fputc(rand()>>16, stdout);
			break;
		}

		// convert to palette colors
		for(y = 0; y < VH; y++)
		for(x = 0; x < VW; x++)
		{
			int r = rawinbuf[y][x][0];
			int g = rawinbuf[y][x][1];
			int b = rawinbuf[y][x][2];
			int cc, r2, g2, b2;
			int cmin = 0;
			float dmin = 1000000.0f;
			float d;

			for (cc = 0; cc < 256; cc++)
			{
				d = cmatch3(r, g, b, palr[cc], palg[cc], palb[cc]);
				if (d < dmin) {
					cmin = cc;
					dmin = d;
				}
			}
			inbuf[y][x] = cmin;
			/*
			r *=   5; g *=   7; b *=   4;
			r += 128; g += 128; b += 128;
			r /= 256; g /= 256; b /= 256;

			assert(r >= 0 && r < 5);
			assert(g >= 0 && g < 8);
			assert(b >= 0 && b < 6);

			int v = r+6*(g+8*b)+16;
			assert(v >= 16 && v <= 255);

			inbuf[y][x] = v;
			*/
		}

		// run algorithm
		algo_1(0x00000000);

		// update ages
		for(y = 0; y < VH; y++)
		for(x = 0; x < VW; x++)
		{
			if(!cmatch2(x, y, lastbuf[y][x], inbuf[y][x]))
			//if(!cmatch(lastbuf[y][x], inbuf[y][x]))
				agebuf[y][x]++;
			else
				agebuf[y][x] = 2;
		}

		// TESTING:
		// convert to rgb24 + write to stdout
		if (dumpfn != NULL)
		{
			for(y = 0; y < VH; y++)
			for(x = 0; x < VW; x++)
			{
				int r, g, b;
				int v = lastbuf[y][x];
				assert(v >= 0 && v <= 255);
	
				rawinbuf[y][x][0] = palr[v];
				rawinbuf[y][x][1] = palg[v];
				rawinbuf[y][x][2] = palb[v];
			}
	
			fwrite(rawinbuf, VW*VH*3, 1, dumpfp);
		}
	}
	
	fprintf(stderr, "**** DONE (%d frames) ****\n", frameno);

	if(fp != NULL)
		fclose(fp);
	if(infp != NULL)
		fclose(infp);
	if(dumpfp != NULL)
		fclose(dumpfp);

	return 0;
}

