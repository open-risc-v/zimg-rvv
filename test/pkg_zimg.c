/* zimg 探针：固定输入的 resize 与 CSC，输出确定性二进制并算 FNV 哈希。
 * 两侧都用 ZIMG_CPU_NONE（强制可移植 C 路径），这样基线与架构无关；
 * 将来接 RVV 内核时改为 ZIMG_CPU_AUTO / 自定义 cpu_type。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zimg.h>

#define W 64
#define H 48
#define OW 96
#define OH 72

static zimg_error_code_e report_error(const char *what)
{
	char msg[256] = { 0 };
	zimg_error_code_e code = zimg_get_last_error(msg, sizeof(msg));
	fprintf(stderr, "%s: %s\n", what, msg);
	return code;
}

static unsigned long long fnv1a(const void *buf, size_t n)
{
	const unsigned char *p = (const unsigned char *)buf;
	unsigned long long h = 0xcbf29ce484222325ULL;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
	return h;
}

static void fill_gray(unsigned char *p, int w, int h, int stride)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			p[(size_t)y * stride + x] = (unsigned char)((x * 3 + y * 5 + ((x * y) >> 3)) & 0xff);
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <out.bin>\n", argv[0]); return 2; }
	FILE *f = fopen(argv[1], "wb");
	if (!f) { perror("fopen"); return 2; }

	/* --- 1) GRAY8 64x48 -> 96x72 bilinear --- */
	unsigned char *src_buf = calloc((size_t)W * H, 1);
	fill_gray(src_buf, W, H, W);
	unsigned char *dst_buf = calloc((size_t)OW * OH, 1);

	zimg_image_format sf, df;
	zimg_image_format_default(&sf, ZIMG_API_VERSION);
	zimg_image_format_default(&df, ZIMG_API_VERSION);
	sf.width = W; sf.height = H; sf.pixel_type = ZIMG_PIXEL_BYTE; sf.depth = 8;
	sf.color_family = ZIMG_COLOR_GREY; sf.subsample_w = 0; sf.subsample_h = 0;
	sf.pixel_range = ZIMG_RANGE_FULL;
	df = sf; df.width = OW; df.height = OH;

	zimg_graph_builder_params params;
	zimg_graph_builder_params_default(&params, ZIMG_API_VERSION);
	params.resample_filter = ZIMG_RESIZE_BILINEAR;
	params.cpu_type = ZIMG_CPU_NONE;

	zimg_filter_graph *g = zimg_filter_graph_build(&sf, &df, &params);
	if (!g) { report_error("resize build"); return 2; }
	size_t tmp_size = 0;
	if (zimg_filter_graph_get_tmp_size(g, &tmp_size)) { report_error("resize tmp_size"); return 2; }
	void *tmp = malloc(tmp_size ? tmp_size : 1);

	zimg_image_buffer_const sb = { ZIMG_API_VERSION };
	zimg_image_buffer db = { ZIMG_API_VERSION };
	sb.plane[0].data = src_buf;  sb.plane[0].stride = (ptrdiff_t)W;  sb.plane[0].mask = ZIMG_BUFFER_MAX;
	db.plane[0].data = dst_buf;  db.plane[0].stride = (ptrdiff_t)OW; db.plane[0].mask = ZIMG_BUFFER_MAX;
	if (zimg_filter_graph_process(g, &sb, &db, tmp, NULL, NULL, NULL, NULL)) {
		report_error("resize process"); return 2;
	}
	fwrite(dst_buf, 1, (size_t)OW * OH, f);
	printf("resize size=%dx%d fnv=%016llx\n", OW, OH, fnv1a(dst_buf, (size_t)OW * OH));
	zimg_filter_graph_free(g);

	/* --- 2) GRAY8 64x48 -> GRAY16（depth 8->16，full range，无抖动） --- */
	unsigned short *p16 = calloc((size_t)W * H, sizeof(unsigned short));

	zimg_image_format s8, d16;
	zimg_image_format_default(&s8, ZIMG_API_VERSION);
	zimg_image_format_default(&d16, ZIMG_API_VERSION);
	s8.width = W; s8.height = H; s8.pixel_type = ZIMG_PIXEL_BYTE; s8.depth = 8;
	s8.color_family = ZIMG_COLOR_GREY; s8.pixel_range = ZIMG_RANGE_FULL;
	d16.width = W; d16.height = H; d16.pixel_type = ZIMG_PIXEL_WORD; d16.depth = 16;
	d16.color_family = ZIMG_COLOR_GREY; d16.pixel_range = ZIMG_RANGE_FULL;

	zimg_graph_builder_params dp;
	zimg_graph_builder_params_default(&dp, ZIMG_API_VERSION);
	dp.dither_type = ZIMG_DITHER_NONE;
	dp.cpu_type = ZIMG_CPU_NONE;

	zimg_filter_graph *g2 = zimg_filter_graph_build(&s8, &d16, &dp);
	if (!g2) { report_error("depth build"); return 2; }
	size_t ts2 = 0;
	if (zimg_filter_graph_get_tmp_size(g2, &ts2)) { report_error("depth tmp_size"); return 2; }
	void *tmp2 = malloc(ts2 ? ts2 : 1);

	zimg_image_buffer_const sb2 = { ZIMG_API_VERSION };
	zimg_image_buffer db2 = { ZIMG_API_VERSION };
	sb2.plane[0].data = src_buf; sb2.plane[0].stride = (ptrdiff_t)W; sb2.plane[0].mask = ZIMG_BUFFER_MAX;
	db2.plane[0].data = p16;     db2.plane[0].stride = (ptrdiff_t)(W * 2); db2.plane[0].mask = ZIMG_BUFFER_MAX;
	if (zimg_filter_graph_process(g2, &sb2, &db2, tmp2, NULL, NULL, NULL, NULL)) {
		report_error("depth process"); return 2;
	}
	fwrite(p16, sizeof(unsigned short), (size_t)W * H, f);
	printf("depth size=%dx%d fnv=%016llx\n", W, H, fnv1a(p16, (size_t)W * H * sizeof(unsigned short)));
	zimg_filter_graph_free(g2);

	fclose(f);
	free(tmp); free(tmp2); free(src_buf); free(dst_buf); free(p16);
	return 0;
}
