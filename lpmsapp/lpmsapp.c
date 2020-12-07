#include "transcoder.h"
#include <sys/stat.h>

#define MAX_PROFILE 13

struct  VideoProfile {
	char	Name[64];
	int		Bitrate;
	int		Framerate;
	int		FramerateDen;
	int		ResolutionW;
	int		ResolutionH;
	char	AspectRatio[64];
};
struct VideoProfile profile[MAX_PROFILE] = {
	{ "P720p60fps16x9", 6000000, 60, 1, 1280, 720, "16:9" },
	{"P720p30fps16x9", 4000000, 30, 1, 1280, 720, "16:9" },
	{"P720p25fps16x9", 3500000, 25, 1, 1280, 720, "16:9" },
	{"P720p30fps4x3", 3500000, 30, 1, 960, 720, "4:3" },
	{"P576p30fps16x9", 1500000, 30, 1, 1024, 576, "16:9" },
	{"P576p25fps16x9", 1500000, 25, 1, 1024, 576, "16:9" },
	{"P360p30fps16x9", 1200000, 30, 1, 640, 360, "16:9" },
	{"P360p25fps16x9", 1000000, 25, 1, 640, 360, "16:9" },
	{"P360p30fps4x3", 1000000, 30, 1, 480, 360, "4:3" },
	{"P240p30fps16x9", 600000, 30, 1, 426, 240, "16:9" },
	{"P240p25fps16x9", 600000, 25, 1, 426, 240, "16:9" },
	{"P240p30fps4x3", 600000, 30, 1, 320, 240, "4:3" },
	{"P144p30fps16x9", 400000, 30, 1, 256, 144,"16:9" }
};

//app input.mp4 output.ts P720p25fps16x9 sw
int main(int argc, char **argv)
{		
	int ret = 0;
	if(argc != 5) return -1;

	int proid = -1;
	//check profile index
	for (int i = 0; i < MAX_PROFILE; i++)
	{
		if (strcmp(argv[3], profile[i].Name) == 0) {
			proid = i;
			break;
		}
	}
	if (proid == -1) {
		printf("\nargument fail!\n");
		return -1;
	}

	struct transcode_thread *h = lpms_transcode_new();
	if (!h) {
		printf("\nmemory alloc fail!\n");
		return -1;
	}
		
	int nout = 1;

	input_params inparam = { argv[1], h, AV_HWDEVICE_TYPE_NONE, NULL };
	output_params* outparam = (output_params*)malloc(sizeof(output_params) * nout);	
	AVRational fps = { profile[proid].Framerate, profile[proid].FramerateDen };
	
	char encoder[64] = { 0, };
	char scale_filter[64] = { 0, };
	char vfilters[128] = { 0, };

	component_opts muxOpts = { "", NULL };
	component_opts audioOpts = { "aac", NULL };
	component_opts vidOpts = { encoder, NULL }; //h264_nvenc	

	if (strcmp(argv[4], "nv") == 0) 
	{ //cuda
		strcpy(vidOpts.name, "h264_nvenc");
		strcpy(scale_filter, "scale_cuda");
	}
	else 
	{ //sw
		strcpy(vidOpts.name, "libx264");
		strcpy(scale_filter, "scale");
	}
	sprintf(vfilters, "%s='w=if(gte(iw,ih),%d,-2):h=if(lt(iw,ih),%d,-2)',fps=%d/%d", 
		scale_filter, profile[proid].ResolutionW, profile[proid].ResolutionH, profile[proid].Framerate, profile[proid].FramerateDen);
	

	output_params param = { argv[2], vfilters, profile[proid].ResolutionW, profile[proid].ResolutionH, profile[proid].Bitrate, 0, fps, muxOpts, audioOpts, vidOpts };
	outparam[0] = param;

	output_results *encresults = (output_results*)malloc(sizeof(output_results) * nout);
	memset(encresults, 0x00, sizeof(output_results)*nout);
	output_results decresults = {0,};

	ret = lpms_transcode(&inparam, outparam, encresults, nout, &decresults);
	if (ret == 0) {
		printf("\ntranscoding success!\n");
	}
	else {
		printf("\ntranscoding fail!\n");
	}
	if (encresults) free(encresults);
	if (outparam) free(outparam);

	lpms_transcode_stop(h);
}