/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*****************************************************************************
 * name:		snd_mem.c
 *
 * desc:		sound caching
 *
 * $Archive: /MissionPack/code/client/snd_mem.c $
 *
 *****************************************************************************/

#include "snd_local.h"
#include "snd_codec.h"
#include "snd_dmahd.h"

#define DEF_COMSOUNDMEGS "8"

/*
===============================================================================

memory management

===============================================================================
*/

static	sndBuffer	*buffer = NULL;
static	sndBuffer	*freelist = NULL;
static	int inUse = 0;
static	int totalInUse = 0;

short *sfxScratchBuffer = NULL;
sfx_t *sfxScratchPointer = NULL;
int	   sfxScratchIndex = 0;


void SND_free( sndBuffer *v )
{
	*(sndBuffer **)v = freelist;
	freelist = (sndBuffer*)v;
	inUse += sizeof(sndBuffer);
	totalInUse -= sizeof(sndBuffer); // -EC-
}


sndBuffer *SND_malloc( void ) {
	sndBuffer *v;

	while ( freelist == NULL )
		S_FreeOldestSound();

	inUse -= sizeof(sndBuffer);
	totalInUse += sizeof(sndBuffer);

	v = freelist;
	freelist = *(sndBuffer **)freelist;
	v->next = NULL;
	return v;
}


void SND_setup( void ) 
{
	sndBuffer *p, *q;
	cvar_t	*cv;
	int scs, sz;
	static int old_scs = -1;

	cv = Cvar_Get( "com_soundMegs", DEF_COMSOUNDMEGS, CVAR_LATCH | CVAR_ARCHIVE );

	scs = (
#ifndef NO_DMAHD
		dmaHD_Enabled() ? (2 * 1536) :
#endif
		(cv->integer * 1536));

	sz = scs * sizeof( sndBuffer );

	// realloc buffer if com_comSoundMegs changed
	if ( old_scs != scs ) {
		if ( buffer != NULL ) {
			free( buffer );
			buffer = NULL;
		}
		old_scs = scs;
	}

	if ( buffer == NULL ) {
		buffer = malloc( sz );
	}

	// -EC-
	if ( buffer == NULL ) {
		Com_Error( ERR_FATAL, "Error allocating %i bytes for sound buffer", sz );
	} else {
		Com_Memset( buffer, 0, sz );
	}

	sz = SND_CHUNK_SIZE * sizeof(short) * 4;

	// allocate the stack based hunk allocator
	// -EC-
	if ( sfxScratchBuffer == NULL ) {
		sfxScratchBuffer = malloc( sz );	//Hunk_Alloc(SND_CHUNK_SIZE * sizeof(short) * 4);
	}

	// clear scratch buffer -EC-
	if ( sfxScratchBuffer == NULL ) {
		Com_Error( ERR_FATAL, "Error allocating %i bytes for sfxScratchBuffer",	sz );
	} else {
		Com_Memset( sfxScratchBuffer, 0, sz );
	}

	sfxScratchPointer = NULL;

	inUse = scs * sizeof( sndBuffer );
	totalInUse = 0; // -EC-

	p = buffer;
	q = p + scs;
	while (--q > p)
		*(sndBuffer **)q = q-1;

	*(sndBuffer **)q = NULL;
	freelist = p + scs - 1;

	Com_Printf( "Sound memory manager started\n" );
}


void SND_shutdown( void )
{
	if ( sfxScratchBuffer ) 
	{
		free( sfxScratchBuffer );
		sfxScratchBuffer = NULL;
	}
	if ( buffer ) 
	{
		free( buffer );
		buffer = NULL;
	}
}


/*
================
ResampleSfx

resample / decimate to the current source rate
================
*/
static int ResampleSfx(sfx_t* sfx, int channels, int inrate, int inwidth, int samples, byte* data, qboolean compressed) {
	int		outcount;
	int		srcsample;
	float	stepscale;
	unsigned int	i, j;
	int		sample, samplefrac, fracstep;
	int			part;
	sndBuffer* chunk;

	//slightly faster and better resample for that case
	if (inrate == 22050 && dma.speed == 44100) {
		sfx->soundLength = sfx->soundLength * 2;
		chunk = sfx->soundData;
		for (i = 0; i < sfx->soundLength; i++) {
			if (inwidth == 2) {
				if ((i & 1) == 0)
					sample = ((short*)data)[i / 2];
				else if (i == (unsigned)(sfx->soundLength - 1))
					sample = (((short*)data)[0] + ((short*)data)[i / 2]) / 2;
				else
					sample = (((short*)data)[i / 2] + ((short*)data)[(i / 2) + 1]) / 2;
			}
			else {
				sample = (unsigned int)((unsigned char)(data[i / 2]) - 128) << 8;
			}
			part = (i & (SND_CHUNK_SIZE - 1));
			if (part == 0) {
				sndBuffer* newchunk;
				newchunk = SND_malloc();
				if (chunk == NULL) {
					sfx->soundData = newchunk;
				}
				else {
					chunk->next = newchunk;
				}
				chunk = newchunk;
			}
			chunk->sndChunk[part] = sample;
		}
	}
	else
	{
		stepscale = (float)inrate / dma.speed;	// this is usually 0.5, 1, or 2

		outcount = samples / stepscale;
		sfx->soundLength = outcount;

		srcsample = 0;
		samplefrac = 0;
		fracstep = stepscale * 256 * channels;
		chunk = sfx->soundData;

		for (i = 0; i < outcount; i++)
		{
			srcsample += samplefrac >> 8;
			//samplefrac &= 255;
			samplefrac += fracstep;
			for (j = 0; j < channels; j++) {
				if (inwidth == 2) {
					sample = (((short*)data)[srcsample + j]);
				}
				else {
					sample = (unsigned int)((unsigned char)(data[srcsample + j]) - 128) << 8;
				}
				part = (i & (SND_CHUNK_SIZE - 1));
				if (part == 0) {
					sndBuffer* newchunk;
					newchunk = SND_malloc();
					if (chunk == NULL) {
						sfx->soundData = newchunk;
					}
					else {
						chunk->next = newchunk;
					}
					chunk = newchunk;
				}

				chunk->sndChunk[part] = sample;
			}
		}
		
	}
	return outcount;
}

/*
================
ResampleSfx

resample / decimate to the current source rate
================
*/
static int ResampleSfxRaw( short *sfx, int channels, int inrate, int inwidth, int samples, byte *data ) {
	int			outcount;
	int			srcsample;
	float		stepscale;
	int			i,j;
	int			sample, samplefrac, fracstep;
	
	stepscale = (float)inrate / dma.speed;	// this is usually 0.5, 1, or 2

	outcount = samples / stepscale;

	srcsample = 0;
	samplefrac = 0;
	fracstep = stepscale * 256 * channels;

	for (i=0 ; i<outcount ; i++)
	{
		srcsample += samplefrac >> 8;
		//samplefrac &= 255;
		samplefrac += fracstep;
		for (j = 0; j < channels; j++) {
			if (inwidth == 2) {
				sample = LittleShort(((short*)data)[srcsample+j]);
			}
			else {
				sample = (int)((unsigned char)(data[srcsample+j]) - 128) << 8;
			}
			sfx[i * channels + j] = sample;
		}
	}
	return outcount;
}

//=============================================================================

/*
==============
S_LoadSound

The filename may be different than sfx->name in the case
of a forced fallback of a player specific sound
==============
*/
qboolean S_LoadSound( sfx_t *sfx )
{
	byte	*data;
	short	*samples;
	snd_info_t	info;
//	int		size;

#ifndef NO_DMAHD
	if (dmaHD_Enabled()) return dmaHD_LoadSound(sfx);
#endif

	// load it in
	data = S_CodecLoad(sfx->soundName, &info);
	if(!data)
		return qfalse;

	if ( info.width == 1 ) {
		Com_DPrintf(S_COLOR_YELLOW "WARNING: %s is a 8 bit wav file\n", sfx->soundName);
	}

	if ( info.rate != 22050 ) {
		Com_DPrintf(S_COLOR_YELLOW "WARNING: %s is not a 22kHz wav file\n", sfx->soundName);
	}

	samples = Hunk_AllocateTempMemory(info.samples * info.samples * sizeof(short) * 2);

	sfx->lastTimeUsed = Com_Milliseconds()+1;

	// each of these compression schemes works just fine
	// but the 16bit quality is much nicer and with a local
	// install assured we can rely upon the sound memory
	// manager to do the right thing for us and page
	// sound in as needed

	if( info.channels == 1 && sfx->soundCompressed == qtrue) {
		sfx->soundCompressionMethod = 1;
		sfx->soundData = NULL;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, data + info.dataofs );
		S_AdpcmEncodeSound(sfx, samples);
#if 0
	} else if (info.channels == 1 && info.samples>(SND_CHUNK_SIZE*16) && info.width >1) {
		sfx->soundCompressionMethod = 3;
		sfx->soundData = NULL;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, (data + info.dataofs) );
		encodeMuLaw( sfx, samples);
	} else if (info.channels == 1 && info.samples>(SND_CHUNK_SIZE*6400) && info.width >1) {
		sfx->soundCompressionMethod = 2;
		sfx->soundData = NULL;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, (data + info.dataofs) );
		encodeWavelet( sfx, samples);
#endif
	} else {
		sfx->soundCompressionMethod = 0;
		sfx->soundLength = ResampleSfx(sfx, info.channels, info.rate, info.width, info.samples, data + info.dataofs, qfalse);
		sfx->soundData = NULL;
	}
	
	sfx->soundChannels = info.channels;

	Hunk_FreeTempMemory(samples);
	Hunk_FreeTempMemory(data);

	return qtrue;
}

void S_DisplayFreeMemory(void) {
	Com_Printf("%d bytes free sound buffer memory, %d total used\n", inUse, totalInUse);
}
