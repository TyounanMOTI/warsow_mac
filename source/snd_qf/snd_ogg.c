/*
Copyright (C) 2002-2003 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_ogg.c

#include "snd_local.h"
#include <vorbis/vorbisfile.h>

#ifdef VORBISLIB_RUNTIME

void *vorbisLibrary = NULL;

int ( *qov_clear )( OggVorbis_File *vf );
int ( *qov_open_callbacks )( void *datasource, OggVorbis_File *vf, char *initial, long ibytes, ov_callbacks callbacks );
ogg_int64_t ( *qov_pcm_total )( OggVorbis_File *vf, int i );
int ( *qov_pcm_seek )( OggVorbis_File *vf, ogg_int64_t pos );
int ( *qov_raw_seek )( OggVorbis_File *vf, ogg_int64_t pos );
ogg_int64_t ( *qov_raw_tell )( OggVorbis_File *vf );
vorbis_info *( *qov_info )( OggVorbis_File *vf, int link );
long ( *qov_read )( OggVorbis_File *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream );

dllfunc_t oggvorbisfuncs[] =
{
	{ "ov_clear", ( void ** )&qov_clear },
	{ "ov_open_callbacks", ( void ** )&qov_open_callbacks },
	{ "ov_pcm_total", ( void ** )&qov_pcm_total },
	{ "ov_raw_seek", ( void ** )&qov_raw_seek },
	{ "ov_raw_tell", ( void ** )&qov_raw_tell },
	{ "ov_info", ( void ** )&qov_info },
	{ "ov_read", ( void ** )&qov_read },
	{ "ov_streams",	( void ** )&qov_streams },
	{ "ov_seekable", ( void ** )&qov_seekable },
	{ "ov_pcm_seek", ( void ** )&qov_pcm_seek },

	{ NULL, NULL }
};

#else // VORBISLIB_RUNTIME

int ( *qov_clear )( OggVorbis_File *vf ) = ov_clear;
int ( *qov_open_callbacks )( void *datasource, OggVorbis_File *vf, char *initial, long ibytes, ov_callbacks callbacks ) = ov_open_callbacks;
ogg_int64_t ( *qov_pcm_total )( OggVorbis_File *vf, int i ) = ov_pcm_total;
int ( *qov_raw_seek )( OggVorbis_File *vf, ogg_int64_t pos ) = ov_raw_seek;
ogg_int64_t ( *qov_raw_tell )( OggVorbis_File *vf ) = ov_raw_tell;
vorbis_info *( *qov_info )( OggVorbis_File *vf, int link ) = ov_info;
long ( *qov_read )( OggVorbis_File *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream ) = ov_read;
long ( *qov_streams )( OggVorbis_File *vf ) = ov_streams;
long ( *qov_seekable )( OggVorbis_File *vf ) = ov_seekable;
int ( *qov_pcm_seek )( OggVorbis_File *vf, ogg_int64_t pos ) = ov_pcm_seek;

#endif // VORBISLIB_RUNTIME

/*
* SNDOGG_Shutdown
*/
void SNDOGG_Shutdown( qboolean verbose )
{
#ifdef VORBISLIB_RUNTIME
	if( vorbisLibrary )
		trap_UnloadLibrary( &vorbisLibrary );
#endif
}

/*
* SNDOGG_Init
*/
void SNDOGG_Init( qboolean verbose )
{
#ifdef VORBISLIB_RUNTIME
	if( vorbisLibrary )
		SNDOGG_Shutdown();

	if( s_vorbis->integer )
	{
		vorbisLibrary = trap_LoadLibrary( VORBISFILE_LIBNAME, oggvorbisfuncs );
		if( vorbisLibrary )
		{
			if( verbose )
				Com_Printf( "Loaded %s\n", VORBISFILE_LIBNAME );
		}
	}
#endif
}

//=============================================================================

/*
* ovcb_read
*/
static size_t ovcb_read( void *ptr, size_t size, size_t nb, void *datasource )
{
	qintptr filenum = (qintptr) datasource;

	return trap_FS_Read( ptr, size * nb, filenum ) / size;
}

/*
* ovcb_seek
*/
static int ovcb_seek( void *datasource, ogg_int64_t offset, int whence )
{
	qintptr filenum = (qintptr) datasource;

	switch( whence )
	{
	case SEEK_SET:
		return trap_FS_Seek( filenum, (int)offset, FS_SEEK_SET );
	case SEEK_CUR:
		return trap_FS_Seek( filenum, (int)offset, FS_SEEK_CUR );
	case SEEK_END:
		return trap_FS_Seek( filenum, (int)offset, FS_SEEK_END );
	}

	return 0;
}

/*
* ovcb_close
*/
static int ovcb_close( void *datasource )
{
	qintptr filenum = (qintptr) datasource;

	trap_FS_FCloseFile( (int) filenum );
	return 0;
}

/*
* ovcb_tell
*/
static long ovcb_tell( void *datasource )
{
	qintptr filenum = (qintptr) datasource;

	return trap_FS_Tell( filenum );
}

static int SNDOGG_FRead( bgTrack_t *track, void *ptr, size_t size );
static int SNDOGG_FSeek( bgTrack_t *track, int pos );
static void SNDOGG_FClose( bgTrack_t *track );

/*
* SNDOGG_Load
*/
sfxcache_t *SNDOGG_Load( sfx_t *s )
{
	OggVorbis_File vorbisfile;
	vorbis_info *vi;
	sfxcache_t *sc;
	char *buffer;
	int filenum, bitstream, bytes_read, bytes_read_total, len, samples;
	ov_callbacks callbacks = { ovcb_read, ovcb_seek, ovcb_close, ovcb_tell };

	assert( s && s->name[0] );
	assert( !s->cache );

#ifdef VORBISLIB_RUNTIME
	if( !vorbisLibrary )
		return NULL;
#endif

	trap_FS_FOpenFile( s->name, &filenum, FS_READ );
	if( !filenum )
		return NULL;

	if( s->isUrl ) {
		callbacks.seek_func = NULL;
		callbacks.tell_func = NULL;
	}

	if( qov_open_callbacks( (void *)(qintptr)filenum, &vorbisfile, NULL, 0, callbacks ) < 0 )
	{
		Com_Printf( "Error getting OGG callbacks: %s\n", s->name );
		trap_FS_FCloseFile( filenum );
		return NULL;
	}

	if( callbacks.seek_func && !qov_seekable( &vorbisfile ) )
	{
		Com_Printf( "Error unsupported .ogg file (not seekable): %s\n", s->name );
		qov_clear( &vorbisfile ); // Does FS_FCloseFile
		return NULL;
	}

	if( qov_streams( &vorbisfile ) != 1 )
	{
		Com_Printf( "Error unsupported .ogg file (multiple logical bitstreams): %s\n", s->name );
		qov_clear( &vorbisfile ); // Does FS_FCloseFile
		return NULL;
	}

	vi = qov_info( &vorbisfile, -1 );
	if( vi->channels != 1 && vi->channels != 2 )
	{
		Com_Printf( "Error unsupported .ogg file (unsupported number of channels: %i): %s\n", vi->channels, s->name );
		qov_clear( &vorbisfile ); // Does FS_FCloseFile
		return NULL;
	}

	samples = (int)qov_pcm_total( &vorbisfile, -1 );
	len = (int) ( (double) samples * (double) dma.speed / (double) vi->rate );
	len = len * 2 * vi->channels;

	sc = s->cache = S_Malloc( len + sizeof( sfxcache_t ) );
	sc->length = samples;
	sc->loopstart = sc->length;
	sc->speed = vi->rate;
	sc->channels = vi->channels;
	sc->width = 2;

	if( sc->speed != dma.speed )
	{
		len = samples * 2 * vi->channels;
		buffer = S_Malloc( len );
	}
	else
	{
		buffer = (char *)sc->data;
	}

	bytes_read = bytes_read_total = 0;
	do
	{
		bytes_read_total += bytes_read;

#ifdef ENDIAN_BIG
		bytes_read = qov_read( &vorbisfile, buffer+bytes_read_total, len-bytes_read_total, 1, 2, 1, &bitstream );
#elif defined (ENDIAN_LITTLE)
		bytes_read = qov_read( &vorbisfile, buffer+bytes_read_total, len-bytes_read_total, 0, 2, 1, &bitstream );
#else
#error "runtime endianess detection support missing"
#endif
	} while( bytes_read > 0 && bytes_read_total < len );

	qov_clear( &vorbisfile ); // Does FS_FCloseFile and also kills vorbis_info vi*

	if( bytes_read_total != len )
	{
		Com_Printf( "Error reading .ogg file: %s\n", s->name );
		if( (void *)buffer != sc->data )
			S_Free( buffer );
		S_Free( sc );
		s->cache = NULL;
		return NULL;
	}

	if( sc->speed != dma.speed ) {
		sc->length = ResampleSfx( samples, sc->speed, sc->channels, 2, (qbyte *)buffer, sc->data, s->name );
		sc->loopstart = sc->speed;
		sc->speed = dma.speed;
	}

	if( (void *)buffer != sc->data ) {
		S_Free( buffer );
	}

	return sc;
}

/*
* SNDOGG_OpenTrack
*/
qboolean SNDOGG_OpenTrack( bgTrack_t *track, qboolean *delay )
{
	int file;
	char path[MAX_QPATH];
	const char *real_path;
	qboolean reopened;
	vorbis_info *vi;
	OggVorbis_File *vf;
	ov_callbacks callbacks = { ovcb_read, ovcb_seek, ovcb_close, ovcb_tell };

#ifdef VORBISLIB_RUNTIME
	if( !vorbisLibrary )
		return qfalse;
#endif
	if( delay )
		*delay = qfalse;
	if( !track )
		return qfalse;

	if( track->file )
	{
		// probably a buffering remote URL, keep the file
		reopened = qtrue;
		file = track->file;
		real_path = track->filename;
	}
	else
	{
		reopened = qfalse;
		if( track->isUrl )
		{
			real_path = path;
			Q_strncpyz( path, track->filename, sizeof( path ) );
			COM_ReplaceExtension( path, ".ogg", sizeof( path ) );
		}
		else
		{
			real_path = track->filename;
		}
		trap_FS_FOpenFile( real_path, &file, FS_READ|FS_NOSIZE );
	}
	if( !file )
		return qfalse;

	track->file = file;
	track->vorbisFile = vf = S_Malloc( sizeof( OggVorbis_File ) );
	track->read = SNDOGG_FRead;
	track->seek = SNDOGG_FSeek;
	track->close = SNDOGG_FClose;
	if( track->isUrl ) {
		callbacks.seek_func = NULL;
		callbacks.tell_func = NULL;
	}

	if( track->isUrl && !reopened )
	{
		if( delay )
			*delay = qtrue;
		return qtrue;
	}

	if( qov_open_callbacks( (void *)(qintptr)track->file, vf, NULL, 0, callbacks ) < 0 )
	{
		Com_Printf( "SNDOGG_OpenTrack: couldn't open %s for reading\n", real_path );
		S_Free( vf );
		trap_FS_FCloseFile( track->file );
		track->file = 0;
		track->vorbisFile = NULL;
		track->read = NULL;
		track->seek = NULL;
		track->close = NULL;
		return qfalse;
	}

	vi = qov_info( vf, -1 );
	if( ( vi->channels != 1 ) && ( vi->channels != 2 ) )
	{
		Com_Printf( "SNDOGG_OpenTrack: %s has an unsupported number of channels: %i\n", real_path, vi->channels );
		qov_clear( vf );
		S_Free( vf );
		track->file = 0;
		track->vorbisFile = NULL;
		track->read = NULL;
		track->seek = NULL;
		track->close = NULL;
		return qfalse;
	}

	track->info.channels = vi->channels;
	track->info.rate = vi->rate;
	track->info.width = 2;
	track->info.dataofs = 0;
	track->info.samples = qov_pcm_total( vf, -1 );
	track->info.loopstart = track->info.samples;

	return qtrue;
}

/*
* SNDOGG_FRead
*/
static int SNDOGG_FRead( bgTrack_t *track, void *ptr, size_t size )
{
	int bs;
	int read;
	int cnt;

	if( !track->vorbisFile )
		return 0;

	cnt = 0;
	do
	{
#ifdef ENDIAN_BIG
		read = qov_read( track->vorbisFile, ( char * )ptr, (int)size, 1, 2, 1, &bs );
#else
		read = qov_read( track->vorbisFile, ( char * )ptr, (int)size, 0, 2, 1, &bs );
#endif
	} while( read == OV_HOLE && cnt++ < 3 );

	if( read < 0 )
		return 0;
	return read;
}

/*
* SNDOGG_FSeek
*/
static int SNDOGG_FSeek( bgTrack_t *track, int pos )
{
	if( !track->vorbisFile )
		return OV_ENOSEEK;
	return qov_pcm_seek( track->vorbisFile, (ogg_int64_t)pos );
}

/*
* SNDOGG_FClose
*/
static void SNDOGG_FClose( bgTrack_t *track )
{
	if( !track->vorbisFile )
		return;
	qov_clear( track->vorbisFile );
	S_Free( track->vorbisFile );
	track->file = 0;
	track->vorbisFile = 0;
}
