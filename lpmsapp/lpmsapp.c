#include "transcoder.h"
#include <sys/stat.h>

#define MAX_PROFILE 13
#define PACKET_VALIDITY 1
/*
decoding and no decoding flag define
1: decoding - yes(compare with W & H after decoding)
0: decoding - no(compare with position and length of AVpacket)
*/
#define PACKET_DECODING 0 
#define MAX_INDEXNUM 20

struct  VideoProfile {
	char	Name[64];
	int		Bitrate;
	int		Framerate;
	int		FramerateDen;
	int		ResolutionW;
	int		ResolutionH;
	char	AspectRatio[64];
};
struct VideoProfile profiles[MAX_PROFILE] = {
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

/*
find index inf profile list
success return 0 >=0 
fail	return < 0
*/
int getprofileID(char* profile) 
{
	int index = -1;
	//check profile index
	for (int i = 0; i < MAX_PROFILE; i++)
	{
		if (strcmp(profile, profiles[i].Name) == 0) {
			index = i;
			break;
		}
	}
	return index;
}
/*
read one packet in video
success return 0
fail	return < 0
*/
int readonepacket(AVFormatContext *fmt_ctx, AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame, int stream_idx)
{
	int ret = 0;
	while (av_read_frame(fmt_ctx, pkt) >= 0) {

		if (pkt->stream_index == stream_idx)
		{
			ret = avcodec_send_packet(dec_ctx, pkt);
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
		}
	}
	return ret;
}
int checkrange(int* indicies, int count, int pkindex, int delta, int* flags)
{
	int ret = -1;
	for (int i = 0; i < count; i++)
	{
		if (flags[i]) continue;
		if (pkindex >= indicies[i] - delta && pkindex < indicies[i] + delta)
		{
			ret = i;
			break;
		}
	}
	return ret;
}

int isindices(int* indicies, int count, int pkindex)
{
	int ret = -1;
	for (int i = 0; i < count; i++)
	{
		if (indicies[i] == pkindex) {
			ret = i;
			break;
		}
	}
	return ret;
}


/*
success return 0
fail	return != 0
*/
int checkValidity(const char* fname, int* indicies, int* pkpos, int* pklength, int count, char* profile)
{
	int bcheck = -1;
	int ret = 0;
	
	//decoding path
	AVFormatContext *fmt_ctx = NULL;
	AVCodec *dec = NULL;
	AVCodecContext *dec_ctx = NULL;
	AVPacket pkt = { 0 };
	AVFrame *frame = NULL;
	int scanflags[MAX_INDEXNUM] = {0,};
	int stream_idx = 0;
	int oknum = 0;
	

	int proid = getprofileID(profile);
	if (proid < 0) {
		fprintf(stderr, "Could not find profile is %s\n", profile);
		return -1;
	}

	int proW = profiles[proid].ResolutionW;
	int proH = profiles[proid].ResolutionH;
	int scandelta = 100;// profiles[proid].Framerate;

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
#if PACKET_DECODING
	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate frame\n");
		ret = AVERROR(ENOMEM);		
		return -1;
	}
	//if could decide first frame
	if (readonepacket(fmt_ctx, dec_ctx, &pkt, frame, stream_idx) == 0)
	{
		av_packet_unref(&pkt);
		//decoding indices frames		
		FILE *fvideo = fopen(fname, "rb");
		
		for (int i = 0; i < count; i++)
		{
			pkt.data = (uint8_t*)malloc(pklength[i]);
			if (fvideo) {
				fseek(fvideo, pkpos[i], SEEK_SET);
				fread(pkt.data, 1, pklength[i], fvideo);
				fclose(fvideo);
			}
			pkt.size = pklength[i];

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
						//debug
						fprintf(stderr, "width = %d height = %d\n", frame->width, frame->height);
						if (proW == frame->width && proH == frame->height) {
							oknum++;
						}
					}
					av_frame_free(&frame);
				}
			}

			if (pkt.data) free(pkt.data);
		}
		//check flag for validity packets
		if(oknum == count)
			bcheck = 0;
		
		if (fvideo) close(fvideo);
	}
	else
	{
		bcheck = -1;
	}
#else
	//only read and compare positions and lengths without decoding
	/* read all packets */
	int packetcount = 0;
	int tmpid = -1;
	while (1) {
		if ((ret = av_read_frame(fmt_ctx, &pkt)) < 0)
			break;
		packetcount++;
		//get index
		//tmpid = checkrange(indicies, count, packetcount, scandelta, scanflags);
		tmpid = isindices(indicies, count, packetcount);
		//for debug
		//printf("packet info: id: %d, pos: %d len: %d\n", packetcount, pkt.pos, pkt.size);
		//compare position and length
		if (tmpid != -1) {
			if (pkt.pos == pkpos[tmpid] && pkt.size == pklength[tmpid]) {
				scanflags[tmpid] = 1;
			}
		}		
		av_packet_unref(&pkt);
	}
	
	for (int i = 0; i < count; i++)
	{
		if (scanflags[i] == 1) oknum++;
	}
	if (oknum == count)
		bcheck = 0;
#endif
	//free buffers
	if(dec_ctx)
		avcodec_free_context(&dec_ctx);
	if (fmt_ctx)
		avformat_close_input(&fmt_ctx);	

	return bcheck;
}
int parseparam(char* strlists, int* pout)
{
	int tmpindnum = 0;
	if (strlen(strlists) > 0) {
		char tmpid[10];
		char* pstmp = strlists;
		int prepos = 0;
		int slen = 0;
		for (int i = 0; i < strlen(strlists); i++)
		{
			if (pstmp[i] == ',' || i == strlen(strlists) - 1) {
				slen = i - prepos;
				if (i == strlen(strlists) - 1) slen++;
				memset(tmpid, 0x00, 10);
				strncpy(tmpid, pstmp + prepos, slen);
				pout[tmpindnum] = atoi(tmpid);
				//av_log(NULL, AV_LOG_ERROR, "oscar --- index:%d: %d\n", tmpindnum, tmpindices[tmpindnum]);
				tmpindnum++;
				prepos = i + 1;
			}
		}
	}
	return tmpindnum;
}
//app input.mp4 output.ts P720p25fps16x9 sw
//app input.mp4 indicies(8,13,...) positions(147956,172020,...) lengths(1504,1316,...) count_of_indicies profilename(P720p30fps16x9)
int main(int argc, char **argv)
{		
#ifdef PACKET_VALIDITY

	int ret = 0;
	if (argc != 7) return -1;

	int indicies[MAX_INDEXNUM] = { 0, };
	int positions[MAX_INDEXNUM] = { 0, };
	int lengths[MAX_INDEXNUM] = { 0, };
	int count = atoi(argv[5]);

	if (parseparam(argv[2], indicies) != count || parseparam(argv[3], positions) != count ||
		parseparam(argv[4], lengths) != count) {
		printf("\n indicies or position,lengths parameter is error!\n");
		return -1;
	}

	ret  = checkValidity(argv[1], indicies, positions, lengths, count, argv[6]);
	if (ret == 0)
	{
		printf("\nredition validity is OK!\n");
	}
	else
	{
		printf("\nredition validity is NG\n");
	}
#else
	int ret = 0;
	if(argc != 5) return -1;

	int proid = -1;
	//check profile index
	for (int i = 0; i < MAX_PROFILE; i++)
	{
		if (strcmp(argv[3], profiles[i].Name) == 0) {
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