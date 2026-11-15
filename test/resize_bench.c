/* zimg resize 整链基准：GRAY16 大图 bicubic 缩放，CPU_NONE / CPU_AUTO 可切。
 * 用 WORD(16bit) 输入是因为 vertical resize 的定点 u16 路径才是主体
 * （u8 在 zimg 里会被提升，走不到这条内核）。 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zimg.h>

static unsigned long long fnv1a(const void *buf, size_t n)
{
	const unsigned char *p = (const unsigned char *)buf;
	unsigned long long h = 0xcbf29ce484222325ULL;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
	return h;
}

static double now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s <none|auto> <reps> [sw sh dw dh]\n", argv[0]); return 2; }
	int auto_cpu = strcmp(argv[1], "auto") == 0;
	int reps = atoi(argv[2]);
	unsigned sw = argc > 3 ? (unsigned)atoi(argv[3]) : 1920;
	unsigned sh = argc > 4 ? (unsigned)atoi(argv[4]) : 1080;
	unsigned dw = argc > 5 ? (unsigned)atoi(argv[5]) : 1280;
	unsigned dh = argc > 6 ? (unsigned)atoi(argv[6]) : 720;

	uint16_t *src = (uint16_t *)malloc((size_t)sw * sh * sizeof(uint16_t));
	uint16_t *dst = (uint16_t *)malloc((size_t)dw * dh * sizeof(uint16_t));
	for (unsigned y = 0; y < sh; y++)
		for (unsigned x = 0; x < sw; x++)
			src[(size_t)y * sw + x] = (uint16_t)((x * 7 + y * 13 + ((x * y) >> 5)) & 0xffff);

	zimg_image_format sf, df;
	zimg_image_format_default(&sf, ZIMG_API_VERSION);
	zimg_image_format_default(&df, ZIMG_API_VERSION);
	sf.width = sw; sf.height = sh; sf.pixel_type = ZIMG_PIXEL_WORD; sf.depth = 16;
	sf.color_family = ZIMG_COLOR_GREY; sf.subsample_w = 0; sf.subsample_h = 0;
	sf.pixel_range = ZIMG_RANGE_FULL;
	df = sf; df.width = dw; df.height = dh;

	zimg_graph_builder_params params;
	zimg_graph_builder_params_default(&params, ZIMG_API_VERSION);
	params.resample_filter = ZIMG_RESIZE_BICUBIC;
	params.cpu_type = auto_cpu ? ZIMG_CPU_AUTO : ZIMG_CPU_NONE;

	zimg_filter_graph *g = zimg_filter_graph_build(&sf, &df, &params);
	if (!g) { char msg[256] = {0}; zimg_get_last_error(msg, sizeof(msg)); printf("FAIL build: %s\n", msg); return 1; }
	size_t tmp_size = 0;
	if (zimg_filter_graph_get_tmp_size(g, &tmp_size)) { printf("FAIL tmp_size\n"); return 1; }
	void *tmp = (void *)malloc(tmp_size ? tmp_size : 1);

	zimg_image_buffer_const sb = { ZIMG_API_VERSION };
	zimg_image_buffer db = { ZIMG_API_VERSION };
	sb.plane[0].data = src; sb.plane[0].stride = (ptrdiff_t)sw * (ptrdiff_t)sizeof(uint16_t); sb.plane[0].mask = ZIMG_BUFFER_MAX;
	db.plane[0].data = dst; db.plane[0].stride = (ptrdiff_t)dw * (ptrdiff_t)sizeof(uint16_t); db.plane[0].mask = ZIMG_BUFFER_MAX;

	if (zimg_filter_graph_process(g, &sb, &db, tmp, NULL, NULL, NULL, NULL)) { printf("FAIL process\n"); return 1; }

	unsigned long long h = fnv1a(dst, (size_t)dw * dh * sizeof(uint16_t));
	double best = 1e18;
	for (int r = 0; r < reps; r++) {
		double t0 = now_us();
		if (zimg_filter_graph_process(g, &sb, &db, tmp, NULL, NULL, NULL, NULL)) { printf("FAIL process\n"); return 1; }
		double t1 = now_us();
		if (t1 - t0 < best) best = t1 - t0;
	}

	printf("cpu=%s size=%ux%u->%ux%u fnv=%016llx best_us=%.0f\n",
	       auto_cpu ? "auto" : "none", sw, sh, dw, dh, h, best);

	free(tmp); zimg_filter_graph_free(g); free(src); free(dst);
	return 0;
}
