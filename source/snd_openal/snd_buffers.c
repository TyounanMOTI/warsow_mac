/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)

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
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "snd_local.h"

#define MAX_SFX 4096
static sfx_t knownSfx[MAX_SFX];
static qboolean buffers_inited = qfalse;

static int s_registration_sequence = 1;
static qboolean s_registering;

/*
* Local helper functions
*/

void * stereo_mono( void *data, snd_info_t *info )
{
	int i, interleave, gain;
	void *outdata;

	outdata = S_Malloc( info->samples * info->width );
	interleave = info->channels * info->width;
	gain = s_stereo2mono->integer;
	clamp( gain, -1, 1 );

	if( info->width == 2 )
	{
		short *pin, *pout;

		pin = (short*)data;
		pout = (short*)outdata;

		for( i = 0; i < info->size; i += interleave, pin += info->channels, pout++ )
		{
			*pout = ((1-gain) * pin[0] + (1+gain) * pin[1]) / 2;
		}
	}
	else if( info->width == 1 )
	{
		char *pin, *pout;

		pin = (char*)data;
		pout = (char*)outdata;

		for( i = 0; i < info->size; i += interleave, pin += info->channels, pout++ )
		{
			*pout = ((1-gain) * pin[0] + (1+gain) * pin[1]) / 2;
		}
	}
	else
	{
		S_Free( outdata );
		return NULL;
	}

	info->channels = 1;
	info->size = info->samples * info->width;

	return outdata;
}

static sfx_t *buffer_find_free( void )
{
	int i;

	for( i = 0; i < MAX_SFX; i++ )
	{
		if( knownSfx[i].filename[0] == '\0' )
			return &knownSfx[i];
	}

	S_Error( "Sound Limit Exceeded.\n" );
	return NULL;
}

// Find a sound effect if loaded, set up a handle otherwise
static sfx_t *buffer_find( const char *filename )
{
	sfx_t *sfx;
	int i;

	for( i = 0; i < MAX_SFX; i++ )
	{
		if( !Q_stricmp( knownSfx[i].filename, filename ) )
			return &knownSfx[i];
	}

	sfx = buffer_find_free();

	memset( sfx, 0, sizeof( *sfx ) );
	Q_strncpyz( sfx->filename, filename, sizeof( sfx->filename ) );

	return sfx;
}

static qboolean buffer_unload( sfx_t *sfx )
{
	ALenum error;

	if( sfx->filename[0] == '\0' || sfx->isLocked || !sfx->inMemory )
		return qfalse;

	qalDeleteBuffers( 1, &sfx->buffer );
	if( ( error = qalGetError() ) != AL_NO_ERROR )
	{
		Com_Printf( "Couldn't delete sound buffer for %s (%s)", sfx->filename, S_ErrorMessage( error ) );
		sfx->isLocked = qtrue;
		return qfalse;
	}

	sfx->inMemory = qfalse;

	return qtrue;
}

// Remove the least recently used sound effect from memory
static qboolean buffer_evict()
{
	int i;
	int candinate = -1;
	int candinate_value = trap_Milliseconds();

	for( i = 0; i < MAX_SFX; i++ )
	{
		if( knownSfx[i].filename[0] == '\0' || !knownSfx[i].inMemory || knownSfx[i].isLocked )
			continue;

		if( knownSfx[i].used < candinate_value )
		{
			candinate = i;
			candinate_value = knownSfx[i].used;
		}
	}

	if( candinate != -1 )
	{
		return buffer_unload( &knownSfx[candinate] );
	}

	return qfalse;
}

static qboolean buffer_load( sfx_t *sfx )
{
	ALenum error;

	void *data;
	snd_info_t info;
	ALuint format;

	if( sfx->filename[0] == '\0' || sfx->inMemory )
		return qfalse;

	data = S_LoadSound( sfx->filename, &info );
	if( !data )
	{
		//Com_DPrintf( "Couldn't load %s\n", sfx->filename );
		return qfalse;
	}

	if( info.channels > 1 )
	{
		void *temp = stereo_mono( data, &info );
		if( temp )
		{
			S_Free( data );
			data = temp;
		}
	}

	format = S_SoundFormat( info.width, info.channels );

	qalGenBuffers( 1, &sfx->buffer );
	if( ( error = qalGetError() ) != AL_NO_ERROR )
	{
		S_Free( data );
		Com_Printf( "Couldn't create a sound buffer for %s (%s)\n", sfx->filename, S_ErrorMessage( error ) );
		return qfalse;
	}

	qalBufferData( sfx->buffer, format, data, info.size, info.rate );
	error = qalGetError();

	// If we ran out of memory, start evicting the least recently used sounds
	while( error == AL_OUT_OF_MEMORY )
	{
		if( !buffer_evict() )
		{
			S_Free( data );
			Com_Printf( "Out of memory loading %s\n", sfx->filename );
			return qfalse;
		}

		// Try load it again
		qalGetError();
		qalBufferData( sfx->buffer, format, data, info.size, info.rate );
		error = qalGetError();
	}

	// Some other error condition
	if( error != AL_NO_ERROR )
	{
		S_Free( data );
		Com_Printf( "Couldn't fill sound buffer for %s (%s)", sfx->filename, S_ErrorMessage( error ) );
		return qfalse;
	}

	S_Free( data );
	sfx->inMemory = qtrue;

	return qtrue;
}

/*
* Sound system wide functions (snd_al_local.h)
*/

qboolean S_InitBuffers( void )
{
	if( buffers_inited )
		return qtrue;

	memset( knownSfx, 0, sizeof( knownSfx ) );

	buffers_inited = qtrue;
	s_registration_sequence = 1;
	s_registering = qfalse;
	return qtrue;
}

void S_ShutdownBuffers( void )
{
	int i;

	if( !buffers_inited )
		return;

	for( i = 0; i < MAX_SFX; i++ )
		buffer_unload( &knownSfx[i] );

	s_registering = qfalse;

	memset( knownSfx, 0, sizeof( knownSfx ) );
	buffers_inited = qfalse;
}

void S_SoundList( void )
{
	int i;

	for( i = 0; i < MAX_SFX; i++ )
	{
		if( knownSfx[i].filename[0] != '\0' )
		{
			if( knownSfx[i].isLocked )
				Com_Printf( "L" );
			else
				Com_Printf( " " );

			if( knownSfx[i].inMemory )
				Com_Printf( "M" );
			else
				Com_Printf( " " );

			Com_Printf( " : %s\n", knownSfx[i].filename );
		}
	}
}

void S_UseBuffer( sfx_t *sfx )
{
	if( sfx->filename[0] == '\0' )
		return;

	if( !sfx->inMemory )
		buffer_load( sfx );

	sfx->used = trap_Milliseconds();
}

ALuint S_GetALBuffer( const sfx_t *sfx )
{
	return sfx->buffer;
}

/**
* Global functions (sound.h)
*/

sfx_t *S_RegisterSound( const char *name )
{
	sfx_t *sfx;

	sfx = buffer_find( name );

	if( !sfx->inMemory )
	{
		if( !buffer_load( sfx ) )
		{
			sfx->filename[0] = '\0';
			sfx->registration_sequence = 0;
			sfx->used = 0;
			return NULL;
		}
	}

	sfx->used = trap_Milliseconds();
	sfx->registration_sequence = s_registration_sequence;
	return sfx;
}

void S_BeginRegistration( void )
{
	s_registration_sequence++;
	if( !s_registration_sequence ) {
		s_registration_sequence = 1;
	}
	s_registering = qtrue;
}

void S_EndRegistration( void )
{
	int i;

	s_registering = qfalse;

	if( !buffers_inited )
		return;

	for( i = 0; i < MAX_SFX; i++ ) {
		if( knownSfx[i].filename[0] == '\0' ) {
			continue;
		}
		if( knownSfx[i].registration_sequence == s_registration_sequence ) {
			if ( !knownSfx[i].inMemory ) {
				buffer_load( &knownSfx[i] );
			}
			continue;
		}
		buffer_unload( &knownSfx[i] );
	}
}

void S_FreeSounds()
{
	S_ShutdownBuffers();
	S_InitBuffers();
}

void S_Clear()
{
}
