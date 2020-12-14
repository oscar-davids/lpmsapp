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
//app input.mp4 position length 
#define PACKET_VALIDITY 1
/*
if success return 1
else return 0
*/
int checkValidity(const char* fname, int pkpos, int pklength)
{
	int bret = 0;
	int ret = 0;
	AVPacket pkt = { 0 };
	AVCodec *dec;
	AVCodecContext *dec_ctx = NULL;
	AVFrame *frame = NULL;
	AVFormatContext *fmt_ctx = NULL;
	int stream_idx = 0;

	if (avformat_open_input(&fmt_ctx, fname, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", fname);
		return -1;
	}

	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		return -1;
	}

	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream in input file '%s'\n",
			av_get_media_type_string(AVMEDIA_TYPE_VIDEO), fname);
		return ret;
	}

	stream_idx = ret;

	dec_ctx = avcodec_alloc_context3(dec);

	if (!dec_ctx) {
		fprintf(stderr, "failed to allocate codec\n");
		return AVERROR(EINVAL);
	}

	ret = avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[stream_idx]->codecpar);
	if (ret < 0) {
		fprintf(stderr, "Failed to copy codec parameters to codec context\n");
		return ret;
	}

	av_dump_format(fmt_ctx, 0, fname, 0);
	if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder\n");
		return ret;
	}

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate frame\n");
		ret = AVERROR(ENOMEM);
		//goto end;
		return -1;
	}
	//decode once
	while (av_read_frame(fmt_ctx, &pkt) >= 0) {

		if (pkt.stream_index == stream_idx)
		{
			//ret = decode_packet(&pkt);
			ret = avcodec_send_packet(dec_ctx, &pkt);
			if (ret < 0) {
				fprintf(stderr, "Error while sending a packet to the decoder\n");
				return ret;
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(dec_ctx, frame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					ret = 0;
					break;
				}
			}

			break;
		}
	}

	// second packet 
	FILE *fvideo = fopen(fname, "rb");
	pkt.data = (uint8_t*)malloc(pklength);
	if (fvideo) {
		fseek(fvideo, pkpos, SEEK_SET);
		fread(pkt.data, 1, pklength, fvideo);
		fclose(fvideo);
	}
	pkt.size = pklength;

	ret = avcodec_send_packet(dec_ctx, &pkt);

	if (ret >= 0)
	{
		while (ret >= 0) {
			ret = avcodec_receive_frame(dec_ctx, frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				ret = 0;
				break;
			}
			else if (ret < 0) {
				fprintf(stderr, "Error while receiving a frame from the decoder\n");
				break;
			}
			if (frame->pict_type == AV_PICTURE_TYPE_I || frame->pict_type == AV_PICTURE_TYPE_B ||
				frame->pict_type == AV_PICTURE_TYPE_P)
			{
				bret = 1;
				fprintf(stderr, "width = %d height = %d\n", frame->width, frame->height);
			}
		}
	}	

	if (fvideo) close(fvideo);
	if (pkt.data) free(pkt.data);
		
	return bret;
}

int main(int argc, char **argv)
{		
#ifdef PACKET_VALIDITY
#else
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
#endif
}