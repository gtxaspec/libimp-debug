/*
 * libimp-debug - IMP debug tool for Ingenic SoCs
 *
 * Communicates with the IMP (Ingenic Media Platform) daemon running on Ingenic
 * IP cameras via POSIX shared memory and semaphores. Compatible with all SoCs
 * that use the IMP framework (T20, T21, T23, T30, T31, T32, T40, T41, A1).
 *
 * Protocol: writes a command struct to shared memory "imp_deubg_shm" (20KB),
 * signals "imp_deubg_sem_tos" (tool→server), waits on "imp_deubg_sem_toc"
 * (server→tool completion), then reads the response from shared memory.
 *
 * Clean-room reimplementation based on protocol analysis.
 */

#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SHM_NAME  "imp_deubg_shm"
#define SEM_TOS   "imp_deubg_sem_tos"
#define SEM_TOC   "imp_deubg_sem_toc"
#define SHM_SIZE  0x5000

/* Command struct offsets (uint32 array in shared memory) */
#define CMD_MODULE    0   /* module: 1=FS, 2=ENC, 3=MISC, 4=AI, 5=AO, 6=TUNING */
#define CMD_SUBCMD    1   /* sub-command within module */
#define CMD_DIR       2   /* direction: 1=request */
#define CMD_DATASIZE  3   /* payload data size in bytes */
#define CMD_PARAM0    4   /* first parameter */
#define CMD_PARAM1    5
#define CMD_PARAM2    6
#define CMD_PARAM3    7
#define CMD_RESPONSE  6   /* text response starts at offset 6 (24 bytes in) */

/* Module IDs */
#define MOD_ISP    0
#define MOD_FS     1
#define MOD_ENC    2
#define MOD_MISC   3
#define MOD_AI     4
#define MOD_AO     5
#define MOD_TUNING 6

typedef struct {
	sem_t *sem_tos;   /* tool → server */
	sem_t *sem_toc;   /* server → tool (completion) */
	int    shm_fd;
	void  *shm_ptr;
} imp_ctx_t;

static imp_ctx_t *g_ctx;

static int ctx_init(imp_ctx_t *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
	if (ctx->shm_fd < 0) {
		fprintf(stderr, "shm_open(%s): %s\n", SHM_NAME, strerror(errno));
		fprintf(stderr, "Is the IMP daemon running?\n");
		return -1;
	}

	/* Ensure the shared memory is the expected size */
	if (ftruncate(ctx->shm_fd, SHM_SIZE) < 0) {
		fprintf(stderr, "ftruncate: %s\n", strerror(errno));
		close(ctx->shm_fd);
		return -1;
	}

	ctx->shm_ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
	                     ctx->shm_fd, 0);
	if (ctx->shm_ptr == MAP_FAILED) {
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		close(ctx->shm_fd);
		return -1;
	}

	ctx->sem_tos = sem_open(SEM_TOS, 0);
	if (ctx->sem_tos == SEM_FAILED) {
		fprintf(stderr, "sem_open(%s): %s\n", SEM_TOS, strerror(errno));
		goto fail;
	}

	ctx->sem_toc = sem_open(SEM_TOC, 0);
	if (ctx->sem_toc == SEM_FAILED) {
		fprintf(stderr, "sem_open(%s): %s\n", SEM_TOC, strerror(errno));
		sem_close(ctx->sem_tos);
		goto fail;
	}

	return 0;

fail:
	munmap(ctx->shm_ptr, SHM_SIZE);
	close(ctx->shm_fd);
	return -1;
}

static void ctx_deinit(imp_ctx_t *ctx)
{
	if (ctx->sem_toc)
		sem_close(ctx->sem_toc);
	if (ctx->sem_tos)
		sem_close(ctx->sem_tos);
	if (ctx->shm_ptr && ctx->shm_ptr != MAP_FAILED)
		munmap(ctx->shm_ptr, SHM_SIZE);
	if (ctx->shm_fd >= 0)
		close(ctx->shm_fd);
}

/* Send command and wait for response */
static int cmd_execute(imp_ctx_t *ctx)
{
	if (sem_post(ctx->sem_tos) < 0) {
		fprintf(stderr, "sem_post: %s\n", strerror(errno));
		return -1;
	}
	if (sem_wait(ctx->sem_toc) < 0) {
		fprintf(stderr, "sem_wait: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static uint32_t *shm(imp_ctx_t *ctx)
{
	return (uint32_t *)ctx->shm_ptr;
}

static void cmd_clear(imp_ctx_t *ctx)
{
	memset(ctx->shm_ptr, 0, SHM_SIZE);
}

/* Print text response from shared memory (starts at word offset 6) */
static void print_response(imp_ctx_t *ctx)
{
	const char *resp = (const char *)&shm(ctx)[CMD_RESPONSE];
	if (resp[0])
		printf("%s\n", resp);
}

/* ======================================================================== */
/* Commands                                                                  */
/* ======================================================================== */

static void cmd_fs_info(imp_ctx_t *ctx)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_FS;
	shm(ctx)[CMD_DIR] = 1;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static void cmd_enc_info(imp_ctx_t *ctx, int chn)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_ENC;
	shm(ctx)[CMD_DIR] = 1;
	shm(ctx)[CMD_DATASIZE] = 4;
	shm(ctx)[CMD_PARAM0] = (uint32_t)chn;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static void cmd_enc_rc_s(imp_ctx_t *ctx, int chn, int offset, int size, int data)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_ENC;
	shm(ctx)[CMD_SUBCMD] = 1;
	shm(ctx)[CMD_DIR] = 1;
	shm(ctx)[CMD_DATASIZE] = 0x10;
	shm(ctx)[CMD_PARAM0] = (uint32_t)chn;
	shm(ctx)[CMD_PARAM1] = (uint32_t)offset;
	shm(ctx)[CMD_PARAM2] = (uint32_t)size;
	shm(ctx)[CMD_PARAM3] = (uint32_t)data;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static void cmd_system_info(imp_ctx_t *ctx)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_MISC;
	shm(ctx)[CMD_SUBCMD] = 2;
	shm(ctx)[CMD_DIR] = 1;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static void cmd_misc_simple(imp_ctx_t *ctx, int a, int b, int c, int d)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_MISC;
	shm(ctx)[CMD_SUBCMD] = 3;
	shm(ctx)[CMD_DATASIZE] = 0x10;
	shm(ctx)[CMD_DIR] = 1;
	shm(ctx)[CMD_PARAM0] = (uint32_t)a;
	shm(ctx)[CMD_PARAM1] = (uint32_t)b;
	shm(ctx)[CMD_PARAM2] = (uint32_t)c;
	shm(ctx)[CMD_PARAM3] = (uint32_t)d;
	cmd_execute(ctx);
}

static int cmd_save_pic(imp_ctx_t *ctx, const char *path, int fmt)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_MISC;
	shm(ctx)[CMD_DIR] = 1;
	shm(ctx)[CMD_DATASIZE] = 0x38;
	shm(ctx)[CMD_PARAM0] = (uint32_t)fmt;
	strncpy((char *)&shm(ctx)[CMD_PARAM1], path, 0x31);

	signal(SIGINT, SIG_IGN);
	int rc = cmd_execute(ctx);
	signal(SIGINT, SIG_DFL);

	if (rc < 0)
		return -1;
	if (shm(ctx)[CMD_DATASIZE] != 0) {
		fprintf(stderr, "save_pic failed: ret=%d\n", shm(ctx)[CMD_DATASIZE]);
		return -1;
	}
	return 0;
}

static void cmd_ai_dev_info(imp_ctx_t *ctx)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_AI;
	shm(ctx)[CMD_DIR] = 1;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static void cmd_ai_get_frm(imp_ctx_t *ctx, int chn)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_AI;
	shm(ctx)[CMD_SUBCMD] = 1;
	shm(ctx)[CMD_DIR] = 1;
	shm(ctx)[CMD_DATASIZE] = 4;
	shm(ctx)[CMD_PARAM0] = (uint32_t)chn;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static void cmd_ao_dev_info(imp_ctx_t *ctx)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_AO;
	shm(ctx)[CMD_DIR] = 1;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static void cmd_ao_get_frm(imp_ctx_t *ctx, int chn)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_AO;
	shm(ctx)[CMD_SUBCMD] = 1;
	shm(ctx)[CMD_DIR] = 1;
	shm(ctx)[CMD_DATASIZE] = 4;
	shm(ctx)[CMD_PARAM0] = (uint32_t)chn;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static void cmd_isp_info(imp_ctx_t *ctx)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_SUBCMD] = 1;
	shm(ctx)[CMD_DIR] = 1;
	if (cmd_execute(ctx) == 0)
		print_response(ctx);
}

static int cmd_snap_raw(imp_ctx_t *ctx, int vinum, const char *path,
                        int snap_cnt, int raw_type, int wdr_rawmode)
{
	uint8_t *buf = (uint8_t *)ctx->shm_ptr;

	cmd_clear(ctx);
	/* snap_raw uses byte offsets, not the standard word layout */
	*(uint32_t *)(buf + 0x0C) = 0x90;  /* data_size */
	*(uint32_t *)(buf + 0x08) = 1;     /* direction */
	*(uint32_t *)(buf + 0x10) = (uint32_t)vinum;
	strncpy((char *)(buf + 0x14), path, 0x31);
	*(uint32_t *)(buf + 0x94) = (uint32_t)snap_cnt;
	*(uint32_t *)(buf + 0x98) = (uint32_t)raw_type;
	*(uint32_t *)(buf + 0x9C) = (uint32_t)wdr_rawmode;

	printf("snap_raw: vinum=%d path=%s snap_cnt=%d raw_type=%d wdr=%d\n",
	       vinum, path, snap_cnt, raw_type, wdr_rawmode);

	signal(SIGINT, SIG_IGN);
	int rc = cmd_execute(ctx);
	signal(SIGINT, SIG_DFL);

	if (rc < 0)
		return -1;
	if (*(uint32_t *)(buf + 0x0C) != 0) {
		fprintf(stderr, "snap_raw failed: ret=%d\n",
		        *(uint32_t *)(buf + 0x0C));
		return -1;
	}
	return 0;
}

static int cmd_tuningtool_start(imp_ctx_t *ctx, int status, int link_mode,
                                int port, const char *ip)
{
	cmd_clear(ctx);
	shm(ctx)[CMD_MODULE] = MOD_TUNING;
	shm(ctx)[CMD_DIR] = 1;
	shm(ctx)[CMD_DATASIZE] = 0x8C;
	shm(ctx)[CMD_PARAM0] = (uint32_t)status;
	shm(ctx)[CMD_PARAM1] = (uint32_t)link_mode;
	shm(ctx)[CMD_PARAM2] = (uint32_t)port;
	strncpy((char *)&shm(ctx)[7], ip ? ip : "", 0x7F);

	printf("tuningtool: status=%d link_mode=%d port=%d ip=%s\n",
	       status, link_mode, port, ip ? ip : "");

	signal(SIGINT, SIG_IGN);
	int rc = cmd_execute(ctx);
	signal(SIGINT, SIG_DFL);

	if (rc < 0)
		return -1;
	if (shm(ctx)[CMD_DATASIZE] != 0) {
		fprintf(stderr, "tuningtool_start failed: ret=%d\n",
		        shm(ctx)[CMD_DATASIZE]);
		return -1;
	}
	return 0;
}

/* ======================================================================== */
/* CLI                                                                       */
/* ======================================================================== */

static void usage(const char *prog)
{
	printf("libimp-debug - IMP debug tool for Ingenic SoCs\n\n");
	printf("Usage: %s <command> [args]\n\n", prog);
	printf("Commands:\n");
	printf("  --system_info              System information\n");
	printf("  --fs_info                  Framesource information\n");
	printf("  --enc_info [chn]           Encoder info (channel, default 0)\n");
	printf("  --enc_rc_s chn:off:sz:data Encoder rate control set\n");
	printf("  --save_pic [path]          Save picture to file\n");
	printf("  --ai_dev_info              Audio input device info\n");
	printf("  --ai_get_frm <chn>         Audio input get frame\n");
	printf("  --ao_dev_info              Audio output device info\n");
	printf("  --ao_get_frm <chn>         Audio output get frame\n");
	printf("  --isp_info                 ISP information (T32+)\n");
	printf("  --snap_raw <vinum> [path]  Capture raw ISP frame (T32+)\n");
	printf("  --tuningtool_start <0|1>   Start/stop ISP tuning (T32+)\n");
	printf("  --misccmd a:b:c:d          Send raw misc command\n");
	printf("\nMisc commands (--misccmd):\n");
	printf("  100:<chn>:<fmt>:0          Snap YUV frame (chn 0-2)\n");
	printf("  201:<mode>:0:0             Set ISP running mode (T21/T23/T30)\n");
	printf("  202:<fps_num>:<fps_den>:0  Set sensor FPS (T21/T23)\n");
	printf("  10000:100:0:0              Dump VBM pool info (T21+)\n");
	printf("  10000:800:<val>:0          Set IVS move dump flag\n");
	printf("\nNotes:\n");
	printf("  On T30, --ai_get_frm records audio to a PCM file\n");
}

enum {
	OPT_ENC_INFO = 0,
	OPT_FS_INFO,
	OPT_SAVE_PIC,
	OPT_SYSTEM_INFO,
	OPT_ENC_RC_S,
	OPT_MISCCMD,
	OPT_AI_DEV_INFO,
	OPT_AI_GET_FRM,
	OPT_AO_DEV_INFO,
	OPT_AO_GET_FRM,
	OPT_ISP_INFO,
	OPT_SNAP_RAW,
	OPT_TUNINGTOOL,
};

static struct option long_options[] = {
	{"enc_info",          optional_argument, NULL, 0},
	{"fs_info",           no_argument,       NULL, 0},
	{"save_pic",          optional_argument, NULL, 0},
	{"system_info",       no_argument,       NULL, 0},
	{"enc_rc_s",          required_argument, NULL, 0},
	{"misccmd",           required_argument, NULL, 0},
	{"ai_dev_info",       no_argument,       NULL, 0},
	{"ai_get_frm",        required_argument, NULL, 0},
	{"ao_dev_info",       no_argument,       NULL, 0},
	{"ao_get_frm",        required_argument, NULL, 0},
	{"isp_info",          no_argument,       NULL, 0},
	{"snap_raw",          required_argument, NULL, 0},
	{"tuningtool_start",  required_argument, NULL, 0},
	{NULL, 0, NULL, 0},
};

int main(int argc, char *argv[])
{
	if (argc < 2) {
		usage(argv[0]);
		return 0;
	}

	imp_ctx_t ctx;
	if (ctx_init(&ctx) < 0)
		return 1;
	g_ctx = &ctx;

	int opt_index = 0;
	int c;

	while ((c = getopt_long(argc, argv, "", long_options, &opt_index)) != -1) {
		if (c != 0)
			continue;

		switch (opt_index) {
		case OPT_ENC_INFO: {
			int chn = optarg ? atoi(optarg) : 0;
			cmd_enc_info(&ctx, chn);
			break;
		}
		case OPT_FS_INFO:
			cmd_fs_info(&ctx);
			break;
		case OPT_SAVE_PIC:
			cmd_save_pic(&ctx, optarg ? optarg : "/tmp/snap.jpg", 0);
			break;
		case OPT_SYSTEM_INFO:
			cmd_system_info(&ctx);
			break;
		case OPT_ENC_RC_S: {
			int a = 0, b = 0, sz = 0, d = 0;
			if (sscanf(optarg, "%d:%d:%d:%d", &a, &b, &sz, &d) == 4)
				cmd_enc_rc_s(&ctx, a, b, sz, d);
			else
				fprintf(stderr, "Usage: --enc_rc_s chn:offset:size:data\n");
			break;
		}
		case OPT_MISCCMD: {
			int a = 0, b = 0, sz = 0, d = 0;
			if (sscanf(optarg, "%d:%d:%d:%d", &a, &b, &sz, &d) == 4)
				cmd_misc_simple(&ctx, a, b, sz, d);
			else
				fprintf(stderr, "Usage: --misccmd cmd:p1:p2:p3\n");
			break;
		}
		case OPT_AI_DEV_INFO:
			cmd_ai_dev_info(&ctx);
			break;
		case OPT_AI_GET_FRM: {
			int chn = atoi(optarg);
			cmd_ai_get_frm(&ctx, chn);
			break;
		}
		case OPT_AO_DEV_INFO:
			cmd_ao_dev_info(&ctx);
			break;
		case OPT_AO_GET_FRM: {
			int chn = atoi(optarg);
			cmd_ao_get_frm(&ctx, chn);
			break;
		}
		case OPT_ISP_INFO:
			cmd_isp_info(&ctx);
			break;
		case OPT_SNAP_RAW: {
			int vinum = 0;
			char path[64] = "/tmp/snap.raw";
			/* Parse: vinum [path] */
			vinum = atoi(optarg);
			if (optind < argc && argv[optind][0] != '-')
				strncpy(path, argv[optind++], sizeof(path) - 1);
			cmd_snap_raw(&ctx, vinum, path, 1, 0, 0);
			break;
		}
		case OPT_TUNINGTOOL: {
			int status = atoi(optarg);
			int link_mode = 0, port = 0;
			const char *ip = "";
			if (optind < argc && argv[optind][0] != '-')
				link_mode = atoi(argv[optind++]);
			if (optind < argc && argv[optind][0] != '-')
				port = atoi(argv[optind++]);
			if (optind < argc && argv[optind][0] != '-')
				ip = argv[optind++];
			cmd_tuningtool_start(&ctx, status, link_mode, port, ip);
			break;
		}
		}
	}

	ctx_deinit(&ctx);
	return 0;
}
