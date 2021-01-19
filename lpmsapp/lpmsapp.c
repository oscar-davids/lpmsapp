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
#define PACKET_188LEN	(188)
#define PACKET_204LEN	(204)
#define NULL_PID		(0x1FFF)
#define TS_HEAD_TAG		(0x47)

#define OFFSET_VALID			(0x80000000)
#define DISPLAY_VALID			(0x40000000)
#define GET_VALID				(0x20000000)
#define DELET_VALID				(0x10000000)
#define SAVE_VALID				(0x08000000)
#define PAT_VALID				(0x00000001)
#define PMT_VALID				(0x00000002)
#define CAT_VALID				(0x00000004)
#define NIT_ACTUAL_VALID		(0x00000008)
#define NIT_OTHER_VALID			(0x00000010)
#define BAT_VALID				(0x00000020)
#define SDT_ACTUAL_VALID		(0x00000040)
#define SDT_OTHER_VALID			(0x00000080)
#define EIT_ACTUAL_VALID		(0x00000100)
#define EIT_ACTUAL_SCH_VALID	(0x00000200)
#define TOT_VALID				(0x00000400)
#define TDT_VALID				(0x00000800)


#define PAT_PID					(0x00)
#define CAT_PID					(0x01)
#define NIT_PID					(0x10)
#define SDT_BAT_PID				(0x11)
#define EIT_PID					(0x12)
#define TDT_TOT_PID				(0x14)

int checkvalidation(const char* fname, int* indicies, int* pkpos, int* pklength, int count, char* profile)
{
	int bcheck = -1;
	int ret = 0;

	FILE *fin;
	uint8_t pkt[188];
	uint8_t* payload;
	uint8_t payload_len = 0;
	uint8_t pes[1024 * 4];

	fin = fopen(fname, "rb");
	if (!fin) {
		return -1;
	}

	for (int i = 0; i < count; i++)
	{
		int pkcount = pklength[i] / PACKET_188LEN;

		fseek(fin, pkpos[i], SEEK_SET);

		int pknum = 0;
		unsigned short packet_pid = NULL_PID;
		int vcount = 0;
		int pes_len = -1;
		
		while (fread(pkt, 188, 1, fin) == 1 && pknum < pkcount)
		{
			if (pkt[0] != 0x47) {
				ret = -1;
				break;
			}
			//check pid and psi
			packet_pid = ((pkt[1] & 0x1F) << 8) | (pkt[2] & 0xFF);
			payload = pkt + 4;
			payload_len = PACKET_188LEN - 4;
			//if there is adaptive field
			if ((pkt[3] >> 5) & 1) {
				payload += payload[0] + 1;
			}
			/*
			if (packet_pid == PAT_PID || packet_pid == CAT_PID || packet_pid == NIT_PID || packet_pid == SDT_BAT_PID ||
				packet_pid == EIT_PID || packet_pid == TDT_TOT_PID || packet_pid == 0x1000) continue;
			*/
			//pes check
			if ((pkt[1] >> 6) & 1) {
				/* Validate the start code */
				if (pes_len > 0) {

					if (pes[0] != 0x00 || pes[1] != 0x00 || pes[2] != 0x01)
					{
						ret = -1;
						break;
					}

					if (pes[3] >= 0xE0 && pes[3] <= 0xEF) {
						vcount++;
					}
					if (pes[3] >= 0xC0 && pes[3] <= 0xDF) {
						ret = -1;
						break;
					}
				}
				pes_len = 0;
			}
			if (pes_len >= 0) {

				if (pes_len + payload_len >= 1024 * 4)
				{
					ret = -1;
					break;
				}

				memcpy(&pes[pes_len], payload, payload_len);
				pes_len += payload_len;
			}

			pknum++;
		}
		if (vcount != 1 || ret == -1)
		{
			break;
		}
	}
	
	fclose(fin);

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
//d:/out.ts  10,15 147956,172020 1504,1316 2 P720p60fps16x9
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

	//ret  = checkValidity(argv[1], indicies, positions, lengths, count, argv[6]);
	ret = checkvalidation(argv[1], indicies, positions, lengths, count, argv[6]);
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
	AVRational fps = { profiles[proid].Framerate, profiles[proid].FramerateDen };
	
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
		scale_filter, profiles[proid].ResolutionW, profiles[proid].ResolutionH, profiles[proid].Framerate, profiles[proid].FramerateDen);
	

	output_params param = { argv[2], vfilters, profiles[proid].ResolutionW, profiles[proid].ResolutionH, profiles[proid].Bitrate, 0, fps, muxOpts, audioOpts, vidOpts };
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