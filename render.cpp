#include <Bela.h>
#include <sndfile.h>
#include <resample.h>
#include <string>
#include <cmath>

#define REC_BUFFER_LEN 2048
#define RESAMP_BUFFER_LEN 8192

using namespace std;

// Filename to record to
string gRecordTrackName = "record.wav";

bool gBufferWriting = false; // know when the buffer has written to disk
bool gActiveBuffer = false;

int SRCerr;

float gRecBuffer[2][REC_BUFFER_LEN]; // double buffer, one for recording, the other for resampling
float gResampBuffer[RESAMP_BUFFER_LEN]; // buffer for storing the output of the resample function before writing to disk
double gResampRatio = 1.00;

static SRC_STATE *recSRC = NULL;

int gReadPtr = REC_BUFFER_LEN;

AuxiliaryTask gFillBufferTask;

SNDFILE *recsndfile ; // File we'll record to
SF_INFO recsfinfo ; // Store info about the sound file

int writeAudio(SNDFILE *sndfile, float *buffer, int samples){
	sf_count_t wroteSamples = sf_write_float(sndfile, buffer, samples);
	int err = sf_error(sndfile);
	if (err) {
		rt_printf("write generated error %s : \n", sf_error_number(err));
	}
	return wroteSamples;
}

void fillBuffer(void*) {
	gBufferWriting = true;
    
    int resampleFrames = ((float) REC_BUFFER_LEN * gResampRatio);
    int writeFrames = src_callback_read(recSRC, gResampRatio, resampleFrames, gResampBuffer);
    // rt_printf("Should have resampled %i frames\n", resampleFrames);
    //rt_printf("Resampled to %i frames\n", writeFrames);
    // rt_printf("SRC returned error: %i\n",SRCerr);
    //writeFrames = REC_BUFFER_LEN;
   	writeAudio(recsndfile, gResampBuffer, writeFrames);
}

// record sample rate converter source callback
long recSRCCallback(void *cb_data, float **data) {
	int readframes = REC_BUFFER_LEN;
	*data = gRecBuffer[!gActiveBuffer]; // return pointer to the start of the array
	return readframes;
}

bool setup(BelaContext *context, void *userData)
{
	// Initialise auxiliary tasks
	if((gFillBufferTask = Bela_createAuxiliaryTask(&fillBuffer, 90, "fill-buffer")) == 0)
		return false;
		
	// Initialise record sample rate converter
	
	recSRC = src_callback_new(recSRCCallback, SRC_LINEAR, 1, &SRCerr, NULL);
		
	// Open the record sound file
	recsfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT; // Specify the write format, see sndfile.h for more info.
	recsfinfo.samplerate = context->audioSampleRate ; // Use the Bela context to set sample rate
	recsfinfo.channels = context->audioInChannels ; // Use the Bela context to set number of channels
	if (!(recsndfile = sf_open (gRecordTrackName.c_str(), SFM_WRITE, &recsfinfo))) {
		rt_printf("Couldn't open file %s : %s\n",gRecordTrackName.c_str(),sf_strerror(recsndfile));
	}
	
	// Locate the start of the record file
	sf_seek(recsndfile, 0, SEEK_SET);
	
	// We should see a file with the correct number of channels and zero frames
	rt_printf("Record file contains %i channel(s)\n", recsfinfo.channels);
	
	return true;
}

void render(BelaContext *context, void *userData)
{
	for(unsigned int n = 0; n < context->audioFrames; n++) {
		//record audio interleaved in gRecBuffer
    	for(unsigned int channel = 0; channel < context->audioInChannels; channel++) {
    		// when gRecBuffer is full, switch to other buffer, write audio to disk using the callback
    		if(++gReadPtr >= REC_BUFFER_LEN) {
	            if(!gBufferWriting)
	                rt_printf("Couldn't write buffer in time -- try increasing buffer size");
	            Bela_scheduleAuxiliaryTask(gFillBufferTask);
	            // switch buffer
	            gActiveBuffer = !gActiveBuffer;
	            // clear the buffer writing flag
	            gBufferWriting = false;
	            gReadPtr = 0;
        	}
        	// store the sample from the audioRead buffer in the active buffer
    		gRecBuffer[gActiveBuffer][gReadPtr] = audioRead(context, n, channel);
    	}
    }
}

void cleanup(BelaContext *context, void *userData)
{
	rt_printf("Closing sound files...");
	sf_close (recsndfile);
}