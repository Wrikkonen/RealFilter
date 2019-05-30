#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/*
 * ファイルの存在確認
 * ----
 * fname: ファイル名
 * f()  : 存在フラグを返す（存在するなら1、存在しないなら0）
 */
int isFile(char *fname)
{
	FILE *fp;
	
	fp = fopen(fname, "rb");
	if (fp) fclose(fp);
	return (fp)? 1: 0;
}

/*
 * 画像をファイルから読み込む
 * ----
 * fname        : ファイル名
 * imgv         : 画像データ
 * width, height: 画像サイズ（横幅、高さ）
 * f()          : 成功フラグを返す（成功なら1、失敗なら0）
 */
int readRawFile(char *fname, unsigned short *imgv, int width, int height)
{
	int ret = 0, size = width * height;
	FILE *fp = NULL;
	
	for (;;) {
		if ((fp = fopen(fname, "rb")) == NULL) break;
		fread((void *)imgv, sizeof(unsigned short), size, fp);
		// 正常終了
		ret = 1;
		break;
	}
	if (fp) fclose(fp);
	return ret;
}

/*
 * 画像をファイルへ書き込む
 * ----
 * fname        : ファイル名
 * imgv         : 画像データ
 * width, height: 画像サイズ（横幅、高さ）
 * f()          : 成功フラグを返す（成功なら1、失敗なら0）
 */
int writeRawFile(char *fname, unsigned short *imgv, int width, int height)
{
	int ret = 0, size = width * height;
	FILE *fp = NULL;
	
	for (;;) {
		if ((fp = fopen(fname, "wb")) == NULL) break;
		fwrite(imgv, sizeof(unsigned short), size, fp);
		// 正常終了
		ret = 1;
		break;
	}
	if (fp) fclose(fp);
	return ret;
}

/*
 * 値をリミットを制御する
 * ----
 * value   : 値
 * min, max: 最小値、最大値
 * f()     : 正規化された値
 */
int normalize(int value, int min, int max)
{
	if (value < min) value = min;
	else if (max < value) value = max;
	return value;
}

/*
 * ピクセル値の最小値と最大値を取得する
 * ----
 * imgv         : 画像バッファ
 * width, height: 画像サイズ（横幅、高さ）
 * min, max     : 最小値、最大値
 */
void getRange(unsigned short *imgv, int width, int height, int *minv, int *maxv)
{
	int i, size;
	
	size = width * height;
	*minv = *maxv = (int)imgv[0];
	for (i = 0; i < size; i++) {
		if (imgv[i] < *minv) *minv = imgv[i];
		if (*maxv < imgv[i]) *maxv = imgv[i];
	}
}

/*
 * フィルタテーブル
 * ----
 * rows   : 行数
 * columns: 列数
 * weight : 重み値
 * f()    : 成功フラグを返す（成功なら1、失敗なら0）
 */
#define Cmax 15
typedef struct {
	int row;
	int column;
	double weight[Cmax][Cmax];
} TABLE;

/*
 * 畳み込み積分する
 * ----
 * jmgv, imgv   : 画像データ（出力、入力）
 * width, height: 画像サイズ（横幅、高さ）
 * filter       : フィルタテーブル
 * f()          : 成功フラグを返す（成功なら1、失敗なら0）
 */
void convolution(unsigned short *jmgv, unsigned short *imgv,
				 int width, int height, TABLE *filter)
{
	int i, j, x, y, xh, yh, x0, y0, minv, maxv;
	double sum, factor, level;
	
	// 係数を計算する
	factor = level = 0.0;
	for (i = 0; i < filter->row; i++)
		for (j = 0; j < filter->column; j++)
			factor += filter->weight[j][i];
	if (factor == 0.0) {
		getRange(imgv, width, height, &minv, &maxv);
		level = (double)(maxv + minv) / 2.0;
		factor = 1.0;
	}
	// 畳み込み積分（コンボリューション）を計算する
	xh = filter->column / 2;
	yh = filter->row / 2;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			sum = 0.0;
			for (i = 0; i < filter->row; i++) {
				y0 = normalize(y-yh+i, 0, height-1);
				for (j = 0; j < filter->column; j++) {
					x0 = normalize(x-xh+j, 0, width-1);
					sum += imgv[width*y0+x0]*filter->weight[j][i];
				}
			}
			jmgv[width*y+x] = normalize((int)(sum/factor+level), 0, 65535);
		}
	}
}

/*
 * ファイルから1行取り出す
 * ----
 * str: 読み込んだ1行
 * f(): strからバイトオーダと改行コードを取り除いたもの
 */
char *getLine(char  *str)
{
	int i, j;
	for (i = j = 0; str[i]; i++) {
		if (isprint(str[i])) str[j++] = str[i];
	}
	str[j] = 0;
	return str;
}

/*
 * テーブルを読み込む
 * ----
 * filter: フィルタテーブル
 * fname : ファイル名
 * f()   : 成功フラグを返す（成功なら1、失敗なら0）
 */
int readTable(TABLE *filter, char *fname)
{
	char buf[1024], *top, *p;
	int ret = 0, i, j;
	FILE *fp = NULL;
	
	// 構造体の情報をリセットする
	filter->column = 0;
	filter->row = 0;
	for (j = 0; j < Cmax; j++) {
		for (i = 0; i < Cmax; i++) {
			filter->weight[j][i] = 0.0;
		}
	}
	// テーブルを読み込む
	for (;;) {
		// ファイルを読み込む
		if ((fp = fopen(fname, "r")) == NULL) break;
		// 行ごとに処理する
		for (j = 0; j < Cmax; ) {
			// 1行読み込む
			if (fgets(buf, sizeof(buf)-1, fp) == NULL) break;
			top = getLine(buf);
			if (strlen(top) <= 0) continue;
			// カンマで分割する
			for (i = 0; i < Cmax; i++) {
				// 1つのパラメータのみ取り出す
				if ((p = strtok(top, ",")) == NULL) break;
				//printf("j=%d, i=%d: [%s] %f\n", j, i, p, atof(p));
				filter->weight[j][i] = atof(p);
				top = NULL;	// 次のパラメータを指示する
			}
			if (filter->column < i) filter->column = i;
			j++;
		}
		filter->row = j;
		// 正常終了
		ret = 1;
		break;
	}
	if (fp) fclose(fp);
	return ret;
}

/*
 * テーブルを表示する
 * ----
 * filter: フィルタテーブル
 */
void printTable(TABLE *filter)
{
	int i, j;
	
	for (j = 0; j < filter->row; j++) {
		for (i = 0; i < filter->column; i++) {
			printf("%5.1f ", filter->weight[j][i]);
		}
		printf("\n");
	}
}

/*
 * 画像処理する
 * ----
 * dst   : 出力ファイル名
 * src   : 入力ファイル名
 * width : 入力ファイルの横幅（ピクセル）
 * height: 入力ファイルの縦幅（ピクセル）
 * fil   : フィルタファイル名
 * f()   : 成功フラグを返す（成功なら1、失敗なら0）
 */
int process(char *dst, char *src, int width, int height, char *fil)
{
	int ret = 0, size;
	unsigned short *imgv = NULL, *jmgv = NULL;
	TABLE filter;
	
	if (readTable(&filter, fil) == 0) {
		printf("filter file error\n");
		printTable(&filter);
		return 0;
	}
	printTable(&filter);	// debug
	
	for (;;) {
		// データ領域を確保する
		size = width * height;
		if ((imgv = (unsigned short *)malloc(sizeof(unsigned short)*size)) == NULL) break;
		if ((jmgv = (unsigned short *)malloc(sizeof(unsigned short)*size)) == NULL) break;
		// フィルタ処理
		if (readRawFile(src, imgv, width, height) == 0) break;
		convolution(jmgv, imgv, width, height, &filter);
		if (writeRawFile(dst, jmgv, width, height) == 0) break;
		// 正常終了
		ret = 1;
		break;
	}
	// データ領域を解放する
	if (jmgv) free(jmgv);
	if (imgv) free(imgv);
	return ret;
}

int main(void)
{
	char dst[256], src[256], fil[256];
	int flag, width, height;
	
	printf("入力ファイル： ");		scanf("%s", src);
	if (isFile(src) == 0) {
		printf("ファイル[%s]が見つかりません。\n", src);
		return 0;
	}
	
	printf("横幅（ピクセル）： ");	scanf("%d", &width);
	printf("高さ（ピクセル）： ");	scanf("%d", &height);
	printf("出力ファイル： ");		scanf("%s", dst);
	printf("フィルタファイル： ");	scanf("%s", fil);

	if (isFile(fil) == 0) {
		printf("ファイル[%s]が見つかりません。\n", fil);
		return 0;
	}

	flag = process(dst, src, width, height, fil);
	printf((flag)? ">成功しました。\n": ">失敗しました。\n");
	return 0;
}
